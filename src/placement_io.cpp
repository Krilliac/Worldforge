// ---------------------------------------------------------------------------
// Implementation of the scene-placement text format and the placement
// authoring core (see placement_io.hpp). Floats are written with full
// round-trip precision (max_digits10) so loading reproduces the exact bit
// pattern for any representable value; std::to_string would truncate to 6
// fractional digits and lose data.
//
// Rotation convention (must match asset_loader's doodadMatrix/wmoMatrix): the
// stored Euler triple is applied as  R = Rz(rot[1]) * Ry(rot[0]) * Rx(rot[2])
// in world space, i.e. rot[1] is the yaw about world +Z, rot[2] the first
// (about world X) and rot[0] the second (about world Y) tilt component.
// ---------------------------------------------------------------------------
#include "placement_io.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "coords.hpp"

namespace wf {

namespace {

constexpr const char* kMagic = "WFSCENE 1";

// Format a float with enough precision to round-trip exactly.
std::string fmtFloat(float v) {
    std::ostringstream os;
    os.precision(std::numeric_limits<float>::max_digits10);
    os << v;
    return os.str();
}

// Strip trailing CR/LF/space/tab so records survive Windows line endings and
// stray whitespace.
std::string rtrim(const std::string& s) {
    size_t end = s.size();
    while (end > 0) {
        char c = s[end - 1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') --end;
        else break;
    }
    return s.substr(0, end);
}

// --- placement-core helpers -------------------------------------------------

constexpr double kRadToDeg = 180.0 / kPi;

// xorshift32: tiny, explicit, deterministic-per-seed PRNG. std::mt19937 /
// std::uniform_real_distribution are avoided on purpose: distribution output
// is not pinned by the standard, and paste jitter must be byte-reproducible.
class Rng32 {
public:
    explicit Rng32(uint32_t seed) : s_(seed ? seed : 0x9E3779B9u) {}

    uint32_t nextU32() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }

    // Uniform in [lo, hi). Top 24 bits -> exactly representable float in [0,1).
    float uniform(float lo, float hi) {
        float u = static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
        return lo + (hi - lo) * u;
    }

private:
    uint32_t s_;
};

// Quantize a normalized scale factor through the MDDF uint16 encoding
// (1024 == 1.0). Clamped to [1, 65535] -- a stored 0 would degenerate the
// model to a point.
uint16_t quantizeScale(float s) {
    if (!(s > 0.0f))  return 1;        // NaN / non-positive -> smallest step
    if (s >= 64.0f)   return 0xFFFF;   // 65535/1024, keeps lround in range
    long q = std::lround(static_cast<double>(s) * 1024.0);
    if (q < 1) q = 1;
    return static_cast<uint16_t>(q);
}

// Rebuild rot[0]/rot[2] so the model's local +Z maps onto `normalWorld`,
// keeping the yaw rot[1]. Under R = Rz(yaw)*Ry(b)*Rx(a) the local up axis
// lands at Rz(yaw) * (sin b * cos a, -sin a, cos b * cos a); un-yaw the target
// normal and solve a (stored rot[2]) and b (stored rot[0]) directly. cos a is
// taken >= 0 (tilt within +-90 degrees), which is the sane branch for terrain
// normals. A degenerate normal leaves the rotation untouched.
void alignRotationToNormal(float rot[3], const Vec3& normalWorld) {
    if (length(normalWorld) < 1e-8f) return;
    Vec3 n = normalize(normalWorld);

    double yaw = radians(rot[1]);
    double cy = std::cos(yaw), sy = std::sin(yaw);
    double ux =  cy * n.x + sy * n.y;   // Rz(-yaw) * n
    double uy = -sy * n.x + cy * n.y;
    double uz = n.z;

    double sa = std::min(1.0, std::max(-1.0, -uy));
    rot[2] = static_cast<float>(std::asin(sa) * kRadToDeg);
    rot[0] = static_cast<float>(std::atan2(ux, uz) * kRadToDeg);
}

// Shared body for both snapToGround overloads (DoodadDef and WmoDef expose
// the same pos[3]/rot[3] layout). Only the stored up-axis component pos[1]
// (== world Z, see coords.hpp) is re-homed so the horizontal position bytes
// stay untouched.
template <typename Record>
void snapRecords(std::vector<Record>& records, const HeightFn& height,
                 const NormalFn& normal, bool alignToNormal) {
    for (Record& r : records) {
        Vec3 world = placementToWorld(Vec3{ r.pos[0], r.pos[1], r.pos[2] });
        if (height) {
            world.z  = height(world.x, world.y);
            r.pos[1] = world.z;
        }
        if (alignToNormal && normal)
            alignRotationToNormal(r.rot, normal(world.x, world.y));
    }
}

// Shared body for both replacePreservingTransform overloads: only the model
// reference changes; every transform byte (pos/rot/scale/uniqueId) is kept.
template <typename Record>
int replaceModelRef(std::vector<Record>& records, const std::vector<size_t>& sel,
                    const std::string& newModelPath, uint32_t newNameIndex,
                    uint32_t Record::*indexMember) {
    int replaced = 0;
    for (size_t idx : sel) {
        if (idx >= records.size()) continue;   // stale selection: ignore
        records[idx].modelName    = newModelPath;
        records[idx].*indexMember = newNameIndex;
        ++replaced;
    }
    return replaced;
}

// Component-wise median (upper median for even counts) of one axis.
float medianOf(std::vector<float>& v) {
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

// Escape a CSV field for the ';'-separated sidecar: fields containing the
// separator, quotes, or newlines are double-quoted with quotes doubled.
std::string csvField(const std::string& s) {
    if (s.find_first_of(";\"\r\n") == std::string::npos) return s;
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else          out += c;
    }
    out += '"';
    return out;
}

} // namespace

std::string saveScene(const std::vector<ScenePlacement>& placements) {
    std::ostringstream os;
    os << kMagic << '\n';
    for (const auto& p : placements) {
        os << (p.isWmo ? "WMO" : "M2") << '\t'
           << p.model                  << '\t'
           << fmtFloat(p.pos.x)        << '\t'
           << fmtFloat(p.pos.y)        << '\t'
           << fmtFloat(p.pos.z)        << '\t'
           << fmtFloat(p.rotZ)         << '\t'
           << fmtFloat(p.scale)        << '\n';
    }
    return os.str();
}

std::vector<ScenePlacement> loadScene(const std::string& text) {
    std::vector<ScenePlacement> out;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        std::string line = rtrim(raw);
        if (line.empty())            continue;   // blank line
        if (line == kMagic)          continue;   // header

        // Split on tabs.
        std::vector<std::string> fields;
        std::istringstream ls(line);
        std::string field;
        while (std::getline(ls, field, '\t')) fields.push_back(field);

        // kind, model, x, y, z, rotZ, scale -> 7 fields required.
        if (fields.size() < 7) continue;

        ScenePlacement p;
        p.isWmo = (fields[0] == "WMO");
        p.model = fields[1];
        try {
            p.pos.x = std::stof(fields[2]);
            p.pos.y = std::stof(fields[3]);
            p.pos.z = std::stof(fields[4]);
            p.rotZ  = std::stof(fields[5]);
            p.scale = std::stof(fields[6]);
        } catch (...) {
            continue;   // non-numeric field -> malformed, skip
        }
        out.push_back(std::move(p));
    }
    return out;
}

// ===========================================================================
// UidAllocator
// ===========================================================================

void UidAllocator::scanTile(const std::vector<DoodadDef>& doodads,
                            const std::vector<WmoDef>&    wmos) {
    for (const DoodadDef& d : doodads) max_ = std::max(max_, d.uniqueId);
    for (const WmoDef&    w : wmos)    max_ = std::max(max_, w.uniqueId);
}

int fixDuplicateUids(std::vector<DoodadDef>& doodads,
                     std::vector<WmoDef>&    wmos,
                     UidAllocator&           uids) {
    // Fold both lists first: a replacement id must clear every id present,
    // including ones we have not walked past yet.
    uids.scanTile(doodads, wmos);

    std::unordered_set<uint32_t> seen;
    seen.reserve(doodads.size() + wmos.size());
    int reassigned = 0;
    auto claim = [&](uint32_t& id) {
        if (!seen.insert(id).second) {   // already taken -> collision
            id = uids.next();
            seen.insert(id);
            ++reassigned;
        }
    };
    for (DoodadDef& d : doodads) claim(d.uniqueId);
    for (WmoDef&    w : wmos)    claim(w.uniqueId);
    return reassigned;
}

// ===========================================================================
// Clipboard
// ===========================================================================

Vec3 medianPosition(const std::vector<DoodadDef>& doodads,
                    const std::vector<WmoDef>&    wmos) {
    std::vector<float> xs, ys, zs;
    xs.reserve(doodads.size() + wmos.size());
    ys.reserve(doodads.size() + wmos.size());
    zs.reserve(doodads.size() + wmos.size());
    auto add = [&](const float pos[3]) {
        Vec3 w = placementToWorld(Vec3{ pos[0], pos[1], pos[2] });
        xs.push_back(w.x); ys.push_back(w.y); zs.push_back(w.z);
    };
    for (const DoodadDef& d : doodads) add(d.pos);
    for (const WmoDef&    w : wmos)    add(w.pos);
    if (xs.empty()) return {};
    return { medianOf(xs), medianOf(ys), medianOf(zs) };
}

std::vector<PlacementClip> copySelection(const std::vector<DoodadDef>& doodads,
                                         const std::vector<WmoDef>&    wmos,
                                         const Vec3&                   pivotWorld) {
    std::vector<PlacementClip> clips;
    clips.reserve(doodads.size() + wmos.size());
    for (const DoodadDef& d : doodads) {
        PlacementClip c;
        c.isWmo     = false;
        c.modelPath = d.modelName;
        c.nameIndex = d.mmidIndex;
        c.relPos    = placementToWorld(Vec3{ d.pos[0], d.pos[1], d.pos[2] }) - pivotWorld;
        c.rotDeg    = { d.rot[0], d.rot[1], d.rot[2] };
        c.scale     = d.scale / 1024.0f;
        clips.push_back(std::move(c));
    }
    for (const WmoDef& w : wmos) {
        PlacementClip c;
        c.isWmo     = true;
        c.modelPath = w.modelName;
        c.nameIndex = w.mwidIndex;
        c.relPos    = placementToWorld(Vec3{ w.pos[0], w.pos[1], w.pos[2] }) - pivotWorld;
        c.rotDeg    = { w.rot[0], w.rot[1], w.rot[2] };
        c.scale     = 1.0f;                 // WMOs cannot scale
        c.doodadSet = w.doodadSet;
        c.nameSet   = w.nameSet;
        clips.push_back(std::move(c));
    }
    return clips;
}

std::vector<PlacementClip> copySelection(const std::vector<DoodadDef>& doodads,
                                         const std::vector<WmoDef>&    wmos) {
    return copySelection(doodads, wmos, medianPosition(doodads, wmos));
}

PasteResult paste(const std::vector<PlacementClip>& clips,
                  PasteMode                         mode,
                  const Vec3&                       anchorWorld,
                  const PasteParams&                params,
                  UidAllocator&                     uids,
                  const HeightFn&                   height,
                  const NormalFn&                   normal) {
    PasteResult out;
    if (clips.empty()) return out;

    Rng32 rng(params.seed);
    for (const PlacementClip& c : clips) {
        Vec3 world = anchorWorld + c.relPos;
        if (mode == PasteMode::OnTerrain && height)
            world.z = height(world.x, world.y);

        float rot[3] = { c.rotDeg.x, c.rotDeg.y, c.rotDeg.z };
        if (params.randomRotation)
            rot[1] += rng.uniform(params.minRotDeg, params.maxRotDeg);   // yaw
        if (params.randomTilt) {
            rot[0] += rng.uniform(params.minTiltDeg, params.maxTiltDeg); // pitch
            rot[2] += rng.uniform(params.minTiltDeg, params.maxTiltDeg); // roll
        }
        // Draw the scale jitter unconditionally so the random stream stays
        // aligned per clip regardless of kind; it is only APPLIED to M2s.
        float scaleMul = params.randomScale
                             ? rng.uniform(params.minScale, params.maxScale)
                             : 1.0f;
        if (params.rotateToGround && normal)
            alignRotationToNormal(rot, normal(world.x, world.y));   // keeps yaw

        Vec3 stored = worldToPlacement(world);
        if (!c.isWmo) {
            DoodadDef d;
            d.mmidIndex = c.nameIndex;
            d.uniqueId  = uids.next();
            d.pos[0] = stored.x; d.pos[1] = stored.y; d.pos[2] = stored.z;
            d.rot[0] = rot[0];   d.rot[1] = rot[1];   d.rot[2] = rot[2];
            d.scale  = quantizeScale(c.scale * scaleMul);
            d.modelName = c.modelPath;
            out.doodads.push_back(std::move(d));
        } else {
            WmoDef w;
            w.mwidIndex = c.nameIndex;
            w.uniqueId  = uids.next();
            w.pos[0] = stored.x; w.pos[1] = stored.y; w.pos[2] = stored.z;
            w.rot[0] = rot[0];   w.rot[1] = rot[1];   w.rot[2] = rot[2];
            // extents stay zero: recompute from the model's bounds at save.
            w.doodadSet = c.doodadSet;
            w.nameSet   = c.nameSet;
            w.modelName = c.modelPath;
            out.wmos.push_back(std::move(w));
        }
    }
    return out;
}

// ===========================================================================
// Snap-to-ground / replace-preserving-transform
// ===========================================================================

void snapToGround(std::vector<DoodadDef>& doodads, const HeightFn& height,
                  const NormalFn& normal, bool alignToNormal) {
    snapRecords(doodads, height, normal, alignToNormal);
}

void snapToGround(std::vector<WmoDef>& wmos, const HeightFn& height,
                  const NormalFn& normal, bool alignToNormal) {
    snapRecords(wmos, height, normal, alignToNormal);
}

int replacePreservingTransform(std::vector<DoodadDef>&    doodads,
                               const std::vector<size_t>& selectionIdx,
                               const std::string&         newModelPath,
                               uint32_t                   newNameIndex) {
    return replaceModelRef(doodads, selectionIdx, newModelPath, newNameIndex,
                           &DoodadDef::mmidIndex);
}

int replacePreservingTransform(std::vector<WmoDef>&       wmos,
                               const std::vector<size_t>& selectionIdx,
                               const std::string&         newModelPath,
                               uint32_t                   newNameIndex) {
    return replaceModelRef(wmos, selectionIdx, newModelPath, newNameIndex,
                           &WmoDef::mwidIndex);
}

// ===========================================================================
// CSV sidecar
// ===========================================================================

std::string exportPlacementsCsv(const std::vector<DoodadDef>& doodads,
                                const std::vector<WmoDef>&    wmos,
                                const NameResolver&           nameResolver) {
    std::ostringstream os;
    os << "ModelFile;PosX;PosY;PosZ;RotX;RotY;RotZ;Scale;Kind\n";

    auto resolvedName = [&](bool isWmo, uint32_t nameIndex,
                            const std::string& modelName) -> std::string {
        if (!modelName.empty()) return modelName;
        if (nameResolver)       return nameResolver(isWmo, nameIndex);
        return {};
    };

    for (const DoodadDef& d : doodads) {
        os << csvField(resolvedName(false, d.mmidIndex, d.modelName)) << ';'
           << fmtFloat(d.pos[0]) << ';' << fmtFloat(d.pos[1]) << ';'
           << fmtFloat(d.pos[2]) << ';'
           << fmtFloat(d.rot[0]) << ';' << fmtFloat(d.rot[1]) << ';'
           << fmtFloat(d.rot[2]) << ';'
           << fmtFloat(d.scale / 1024.0f) << ";m2\n";
    }
    for (const WmoDef& w : wmos) {
        os << csvField(resolvedName(true, w.mwidIndex, w.modelName)) << ';'
           << fmtFloat(w.pos[0]) << ';' << fmtFloat(w.pos[1]) << ';'
           << fmtFloat(w.pos[2]) << ';'
           << fmtFloat(w.rot[0]) << ';' << fmtFloat(w.rot[1]) << ';'
           << fmtFloat(w.rot[2]) << ';'
           << "1;wmo\n";                    // WMOs cannot scale
    }
    return os.str();
}

} // namespace wf
