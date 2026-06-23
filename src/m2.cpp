#include "m2.hpp"
#include "byte_reader.hpp"
#include <stdexcept>

namespace wf {
namespace {

// Vanilla MD20 header field offsets (version 0x100). Each "M2Array" is an
// (count, offset) uint32 pair. Verified against getMaNGOS vanilla layout.
constexpr size_t kOffName       = 0x008; // M2Array<char>
constexpr size_t kOffVertices   = 0x044; // M2Array<M2Vertex>
constexpr size_t kOffViews      = 0x04C; // M2Array<M2SkinProfile> (embedded)
constexpr size_t kOffTextures   = 0x05C; // M2Array<M2Texture>

constexpr size_t kVertexStride  = 48;    // M2Vertex
constexpr size_t kTextureStride = 16;    // M2Texture (type, flags, M2Array name)
constexpr size_t kSubmeshStride = 32;    // vanilla M2SkinSection (no sort centre)

struct Arr { uint32_t count; uint32_t offset; };

Arr readArr(ByteReader& r, size_t at) {
    r.seek(at);
    uint32_t count  = r.u32();
    uint32_t offset = r.u32();
    return { count, offset };
}

void requireRange(const std::vector<uint8_t>& buf, uint32_t off, size_t bytes, const char* what) {
    if (static_cast<size_t>(off) + bytes > buf.size())
        throw std::runtime_error(std::string("M2 array out of range: ") + what);
}

std::string readCStr(const std::vector<uint8_t>& buf, uint32_t off, uint32_t count) {
    if (count == 0) return std::string();
    requireRange(buf, off, count, "string");
    const char* p = reinterpret_cast<const char*>(buf.data() + off);
    size_t len = 0;
    while (len < count && p[len] != '\0') ++len;
    return std::string(p, len);
}

} // namespace

M2Model parseM2(const std::vector<uint8_t>& buf) {
    ByteReader r(buf);
    if (buf.size() < 0x100) throw std::runtime_error("M2 too small for a header");
    if (r.fourccRaw() != "MD20") throw std::runtime_error("not an MD20 (M2) file");

    M2Model m;
    r.seek(0x004);
    m.version = r.u32();

    // name
    {
        Arr a = readArr(r, kOffName);
        m.name = readCStr(buf, a.offset, a.count);
    }

    // global vertices
    {
        Arr a = readArr(r, kOffVertices);
        requireRange(buf, a.offset, static_cast<size_t>(a.count) * kVertexStride, "vertices");
        m.vertices.reserve(a.count);
        for (uint32_t i = 0; i < a.count; ++i) {
            ByteReader vr(buf.data() + a.offset + i * kVertexStride, kVertexStride);
            M2Vertex v;
            v.pos = { vr.f32(), vr.f32(), vr.f32() };
            for (uint8_t& w : v.boneWeights) w = vr.u8();
            for (uint8_t& b : v.boneIndices) b = vr.u8();
            v.normal = { vr.f32(), vr.f32(), vr.f32() };
            v.uv = { vr.f32(), vr.f32() };
            m.vertices.push_back(v);
        }
    }

    // textures
    {
        Arr a = readArr(r, kOffTextures);
        requireRange(buf, a.offset, static_cast<size_t>(a.count) * kTextureStride, "textures");
        for (uint32_t i = 0; i < a.count; ++i) {
            ByteReader tr(buf.data() + a.offset + i * kTextureStride, kTextureStride);
            uint32_t type = tr.u32();
            tr.u32(); // flags
            uint32_t nameCount  = tr.u32();
            uint32_t nameOffset = tr.u32();
            m.textureTypes.push_back(type);
            // Only type 0 carries a hardcoded path; others are runtime-substituted.
            m.textures.push_back(type == 0 ? readCStr(buf, nameOffset, nameCount) : std::string());
        }
    }

    // view 0 (embedded skin profile)
    {
        Arr views = readArr(r, kOffViews);
        if (views.count > 0) {
            // M2SkinProfile header: 5 M2Arrays + uint32 boneCountMax = 44 bytes.
            requireRange(buf, views.offset, 44, "view header");
            ByteReader vr(buf.data() + views.offset, 44);
            Arr vlook = { vr.u32(), vr.u32() }; // vertices (lookup)
            Arr tris  = { vr.u32(), vr.u32() }; // indices (triangles)
            vr.u32(); vr.u32();                 // bones (per-vertex bone indices)
            Arr subs  = { vr.u32(), vr.u32() }; // submeshes
            // (batches + boneCountMax follow; not needed for static geometry)

            requireRange(buf, vlook.offset, static_cast<size_t>(vlook.count) * 2, "view vertices");
            m.vertexLookup.reserve(vlook.count);
            { ByteReader lr(buf.data() + vlook.offset, vlook.count * 2);
              for (uint32_t i = 0; i < vlook.count; ++i) m.vertexLookup.push_back(lr.u16()); }

            requireRange(buf, tris.offset, static_cast<size_t>(tris.count) * 2, "view triangles");
            m.triangles.reserve(tris.count);
            { ByteReader tr(buf.data() + tris.offset, tris.count * 2);
              for (uint32_t i = 0; i < tris.count; ++i) m.triangles.push_back(tr.u16()); }

            requireRange(buf, subs.offset, static_cast<size_t>(subs.count) * kSubmeshStride, "submeshes");
            for (uint32_t i = 0; i < subs.count; ++i) {
                ByteReader sr(buf.data() + subs.offset + i * kSubmeshStride, kSubmeshStride);
                M2Submesh s;
                s.id          = sr.u16();
                sr.u16();                 // Level (level<<16 added to start fields)
                s.vertexStart = sr.u16();
                s.vertexCount = sr.u16();
                s.indexStart  = sr.u16();
                s.indexCount  = sr.u16();
                m.submeshes.push_back(s);
            }
        }
    }

    return m;
}

// ===========================================================================
// Animation parsing (vanilla layout)
// ===========================================================================
namespace {

constexpr size_t kOffAnimations = 0x01C; // nAnimations, ofsAnimations
constexpr size_t kOffBones      = 0x034; // nBones, ofsBones

// VERIFY-AGAINST-REAL-FILE constants (vanilla specifics that differ across
// sources). The parser logic below is independent of these; only the byte
// strides depend on them, so a single real 1.12 .m2 confirms/adjusts them:
constexpr size_t kSeqStride   = 0x40;    // ModelAnimation record size
constexpr size_t kBoneStride  = 0x6C;    // ModelBoneDef: 12 hdr + 3*28 tracks + 12 pivot = 108
constexpr size_t kAnimBlock   = 0x1C;    // AnimationBlock: 28 bytes
constexpr size_t kRotStride   = 16;      // rotation quaternion as 4 floats (vanilla)

// Read a vanilla AnimationBlock at `at` into a RawChannel<T>. `readValue`
// decodes one value of stride `valStride` from a ByteReader.
template <typename T, typename F>
RawChannel<T> readChannel(const std::vector<uint8_t>& buf, size_t at,
                          size_t valStride, F readValue) {
    RawChannel<T> ch;
    ByteReader b(buf.data() + at, kAnimBlock);
    ch.interp    = static_cast<uint16_t>(b.u16());
    ch.globalSeq = static_cast<int16_t>(b.u16());
    uint32_t nRanges = b.u32(), ofsRanges = b.u32();
    uint32_t nTimes  = b.u32(), ofsTimes  = b.u32();
    uint32_t nKeys   = b.u32(), ofsKeys   = b.u32();

    if (nRanges && static_cast<size_t>(ofsRanges) + nRanges * 8 <= buf.size()) {
        ByteReader r(buf.data() + ofsRanges, nRanges * 8);
        for (uint32_t i = 0; i < nRanges; ++i) {
            uint32_t a = r.u32(), c = r.u32();
            ch.ranges.emplace_back(a, c);
        }
    }
    if (nTimes && static_cast<size_t>(ofsTimes) + nTimes * 4 <= buf.size()) {
        ByteReader r(buf.data() + ofsTimes, nTimes * 4);
        for (uint32_t i = 0; i < nTimes; ++i) ch.times.push_back(r.u32());
    }
    if (nKeys && static_cast<size_t>(ofsKeys) + nKeys * valStride <= buf.size()) {
        for (uint32_t i = 0; i < nKeys; ++i) {
            ByteReader r(buf.data() + ofsKeys + i * valStride, valStride);
            ch.values.push_back(readValue(r));
        }
    }
    return ch;
}

} // namespace

M2Animation parseM2Animation(const std::vector<uint8_t>& buf) {
    if (buf.size() < 0x148) throw std::runtime_error("M2 too small for vanilla header");
    ByteReader r(buf);
    if (r.fourccRaw() != "MD20") throw std::runtime_error("not an MD20 (M2) file");

    M2Animation out;

    // sequences
    {
        r.seek(kOffAnimations);
        uint32_t n = r.u32(), ofs = r.u32();
        if (static_cast<size_t>(ofs) + static_cast<size_t>(n) * kSeqStride <= buf.size()) {
            for (uint32_t i = 0; i < n; ++i) {
                ByteReader s(buf.data() + ofs + i * kSeqStride, kSeqStride);
                M2Sequence seq;
                seq.id     = s.u16();
                seq.subId  = s.u16();
                seq.length = s.u32();
                s.seek(0x0C); seq.flags = s.u32();   // flags location within record
                out.sequences.push_back(seq);
            }
        }
    }

    // bones
    {
        r.seek(kOffBones);
        uint32_t n = r.u32(), ofs = r.u32();
        auto readVec3 = [](ByteReader& b){ return Vec3{ b.f32(), b.f32(), b.f32() }; };
        auto readQuat = [](ByteReader& b){ return Quat{ b.f32(), b.f32(), b.f32(), b.f32() }; };
        for (uint32_t i = 0; i < n; ++i) {
            size_t base = ofs + i * kBoneStride;
            if (base + kBoneStride > buf.size()) break;
            ByteReader b(buf.data() + base, kBoneStride);
            M2BoneRaw bone;
            b.u32();                                  // keyBoneId
            b.u32();                                  // flags
            bone.parent = static_cast<int16_t>(b.u16());
            b.u16();                                  // submeshId
            bone.translation = readChannel<Vec3>(buf, base + 0x0C,                12, readVec3);
            bone.rotation    = readChannel<Quat>(buf, base + 0x0C + kAnimBlock,   kRotStride, readQuat);
            bone.scale       = readChannel<Vec3>(buf, base + 0x0C + kAnimBlock*2, 12, readVec3);
            ByteReader pv(buf.data() + base + 0x0C + kAnimBlock*3, 12);
            bone.pivot = { pv.f32(), pv.f32(), pv.f32() };
            out.bones.push_back(bone);
        }
    }

    return out;
}

// Slice each bone's raw channels down to one animation, producing pose-ready
// Bones for the animation core. Uses the per-animation interpolation range when
// present; otherwise (global sequence / no ranges) uses the whole track.
std::vector<Bone> buildBonesForAnimation(const M2Animation& anim, int animIndex) {
    std::vector<Bone> bones;
    bones.reserve(anim.bones.size());

    auto sliceVec3 = [&](const RawChannel<Vec3>& ch) -> KeyTrack<Vec3> {
        KeyTrack<Vec3> kt; kt.interp = ch.interp;
        if (ch.times.empty()) return kt;
        size_t first = 0, last = ch.times.size() - 1;
        if (animIndex >= 0 && static_cast<size_t>(animIndex) < ch.ranges.size()) {
            first = ch.ranges[animIndex].first;
            last  = ch.ranges[animIndex].second;
        }
        for (size_t i = first; i <= last && i < ch.times.size(); ++i) {
            kt.times.push_back(ch.times[i]);
            kt.values.push_back(ch.values[i]);
        }
        return kt;
    };
    auto sliceQuat = [&](const RawChannel<Quat>& ch) -> KeyTrack<Quat> {
        KeyTrack<Quat> kt; kt.interp = ch.interp;
        if (ch.times.empty()) return kt;
        size_t first = 0, last = ch.times.size() - 1;
        if (animIndex >= 0 && static_cast<size_t>(animIndex) < ch.ranges.size()) {
            first = ch.ranges[animIndex].first;
            last  = ch.ranges[animIndex].second;
        }
        for (size_t i = first; i <= last && i < ch.times.size(); ++i) {
            kt.times.push_back(ch.times[i]);
            kt.values.push_back(ch.values[i]);
        }
        return kt;
    };

    for (const M2BoneRaw& rb : anim.bones) {
        Bone b;
        b.parent = rb.parent;
        b.pivot  = rb.pivot;
        b.translation = sliceVec3(rb.translation);
        b.rotation    = sliceQuat(rb.rotation);
        b.scale       = sliceVec3(rb.scale);
        bones.push_back(b);
    }
    return bones;
}

} // namespace wf
