#include "adt_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <unordered_map>

#include "byte_writer.hpp"
#include "liquid_edit.hpp"   // encodeMclq / mcnkLiquidFlags

namespace fs = std::filesystem;

namespace wf {

namespace {

// MCNK header field offsets (within the 128-byte header), the write-side twin
// of the table in terrain.cpp. Verified vs ADT/v18.
constexpr size_t kHdrSize        = 128;
constexpr size_t kOffFlags       = 0x00;
constexpr size_t kOffIndexX      = 0x04;
constexpr size_t kOffIndexY      = 0x08;
constexpr size_t kOffNLayers     = 0x0C;
constexpr size_t kOffNDoodadRefs = 0x10;
constexpr size_t kOffOfsMCVT     = 0x14;
constexpr size_t kOffOfsMCNR     = 0x18;
constexpr size_t kOffOfsMCLY     = 0x1C;
constexpr size_t kOffOfsMCRF     = 0x20;
constexpr size_t kOffOfsMCAL     = 0x24;
constexpr size_t kOffSizeMCAL    = 0x28;
constexpr size_t kOffOfsMCSH     = 0x2C;
constexpr size_t kOffSizeMCSH    = 0x30;
constexpr size_t kOffAreaId      = 0x34;
constexpr size_t kOffNMapObjRefs = 0x38;
constexpr size_t kOffHoles       = 0x3C;
constexpr size_t kOffPredTex     = 0x40;
constexpr size_t kOffNoEffectDoodad = 0x50;
constexpr size_t kOffNSndEmitters = 0x58;
constexpr size_t kOffOfsMCSE     = 0x5C;
constexpr size_t kOffOfsMCLQ     = 0x60;
constexpr size_t kOffSizeLiquid  = 0x64;
constexpr size_t kOffPosition    = 0x68;

constexpr size_t kMcinEntries    = 256;   // MCIN is always 256 x 16 bytes
constexpr size_t kMhdrSize       = 64;    // SMMapHeaderObj, vanilla 64 bytes
constexpr size_t kMcshBytes      = 64u * 64u / 8u;   // 512
constexpr size_t kMcnrBytes      = 145u * 3u;        // declared size (pad excluded)
constexpr size_t kMcnrPad        = 13;               // vanilla pad OUTSIDE the size

// Little-endian pokes into an already-emitted buffer (offset fix-up passes).
void poke16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off]     = static_cast<uint8_t>(v & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void poke32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    for (int b = 0; b < 4; ++b)
        buf[off + static_cast<size_t>(b)] = static_cast<uint8_t>((v >> (8 * b)) & 0xFF);
}
void pokeF32(std::vector<uint8_t>& buf, size_t off, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    poke32(buf, off, bits);
}

// Emit [reversed fourcc][u32 size][payload] -- the on-disk chunk framing
// forEachChunk / ByteReader::fourccReversed read back. Returns the offset of
// the chunk's magic within the writer (for MHDR/MCIN/sub-chunk offsets).
uint32_t putChunk(ByteWriter& w, const char* magic, const std::vector<uint8_t>& payload) {
    const uint32_t at = static_cast<uint32_t>(w.size());
    w.u8(static_cast<uint8_t>(magic[3]));
    w.u8(static_cast<uint8_t>(magic[2]));
    w.u8(static_cast<uint8_t>(magic[1]));
    w.u8(static_cast<uint8_t>(magic[0]));
    w.u32(static_cast<uint32_t>(payload.size()));
    if (!payload.empty()) w.bytes(payload.data(), payload.size());
    return at;
}

// Quantise one normal component to the file's int8 form -- must match
// writeAdtNormals (terrain.cpp) exactly so the two write paths agree.
uint8_t normToI8(float c) {
    long q = std::lround(std::clamp(c, -1.0f, 1.0f) * 127.0f);
    return static_cast<uint8_t>(static_cast<int8_t>(std::clamp(q, -127L, 127L)));
}

// MMDX/MMID (or MWMO/MWID) name table, rebuilt from the live placement
// lists: deduplicated (exact match -- the strings came from one blob, so
// case variants cannot diverge within a tile), order = first use.
struct NameTable {
    std::vector<char>     blob;      // NUL-separated names (MMDX/MWMO payload)
    std::vector<uint32_t> offsets;   // per-name byte offset (MMID/MWID payload)
    std::unordered_map<std::string, uint32_t> index;   // name -> table index

    uint32_t add(const std::string& name) {
        auto it = index.find(name);
        if (it != index.end()) return it->second;
        const uint32_t idx = static_cast<uint32_t>(offsets.size());
        offsets.push_back(static_cast<uint32_t>(blob.size()));
        blob.insert(blob.end(), name.begin(), name.end());
        blob.push_back('\0');
        index.emplace(name, idx);
        return idx;
    }

    std::vector<uint8_t> blobBytes() const {
        return std::vector<uint8_t>(blob.begin(), blob.end());
    }
    std::vector<uint8_t> offsetBytes() const {
        ByteWriter w;
        for (uint32_t o : offsets) w.u32(o);
        return std::vector<uint8_t>(w.data());
    }
};

// One MCNK chunk payload: 128-byte header + sub-chunks in canonical order.
// Sub-chunk header offsets are relative to the MCNK data start (i.e. they
// include the 128-byte header), the convention parseOneChunk's magic probe
// resolves with baseAdj == 0 -- the same layout the real 1.12 files use.
std::vector<uint8_t> buildMcnkPayload(const MapChunk& mc) {
    ByteWriter w;
    w.bytes(std::vector<uint8_t>(kHdrSize, 0).data(), kHdrSize);   // header, fixed up last

    // MCVT: 145 floats, relative to header position.z (stored as parsed).
    std::vector<uint8_t> mcvt;
    {
        ByteWriter v;
        for (int i = 0; i < 145; ++i) v.f32(mc.heights[i]);
        mcvt = v.take();
    }
    const uint32_t ofsMcvt = putChunk(w, "MCVT", mcvt);

    // MCNR: 145 int8 triples in file order (x,y,z); the declared size is 435
    // and the 13 pad bytes vanilla appends live OUTSIDE it -- mirror the quirk
    // the parser's offset walk was built to survive.
    std::vector<uint8_t> mcnr;
    mcnr.reserve(kMcnrBytes);
    for (int i = 0; i < 145; ++i) {
        mcnr.push_back(normToI8(mc.normals[i].x));
        mcnr.push_back(normToI8(mc.normals[i].y));
        mcnr.push_back(normToI8(mc.normals[i].z));
    }
    const uint32_t ofsMcnr = putChunk(w, "MCNR", mcnr);
    w.bytes(std::vector<uint8_t>(kMcnrPad, 0).data(), kMcnrPad);

    // MCLY: 16 bytes per layer, exactly the parsed entries (packAlphaLayers /
    // ensureLayer already keep flags + ofsAlpha consistent with mc.alpha).
    std::vector<uint8_t> mcly;
    {
        ByteWriter v;
        for (const TexLayer& l : mc.layers) {
            v.u32(l.textureId);
            v.u32(l.flags);
            v.u32(l.ofsAlpha);
            v.u32(l.effectId);
        }
        mcly = v.take();
    }
    const uint32_t ofsMcly = putChunk(w, "MCLY", mcly);

    // MCRF: doodad (MDDF) indices, then map-object (MODF) indices.
    std::vector<uint8_t> mcrf;
    {
        ByteWriter v;
        for (uint32_t r : mc.doodadRefs) v.u32(r);
        for (uint32_t r : mc.wmoRefs)    v.u32(r);
        mcrf = v.take();
    }
    const uint32_t ofsMcrf = putChunk(w, "MCRF", mcrf);

    // MCAL: the raw blob the MCLY ofsAlpha values index (present even when
    // empty; the parser tolerates a zero-size MCAL at a valid offset).
    const uint32_t ofsMcal  = putChunk(w, "MCAL", mc.alpha);
    const uint32_t sizeMcal = static_cast<uint32_t>(mc.alpha.size());

    // MCSH: 512 bytes when the chunk carries a shadow map (short parses were
    // zero-padded at read time; enforce the full size on the way out).
    uint32_t ofsMcsh = 0, sizeMcsh = 0;
    if (!mc.shadow.empty()) {
        std::vector<uint8_t> mcsh = mc.shadow;
        mcsh.resize(kMcshBytes, 0);
        ofsMcsh  = putChunk(w, "MCSH", mcsh);
        sizeMcsh = static_cast<uint32_t>(kMcshBytes);
    }

    // MCLQ: one 804-byte block per liquid layer in LQ-flag order; the header
    // sizeLiquid counts the chunk framing too (bytes + 8), 0 when dry.
    const std::vector<uint8_t> mclq = encodeMclq(mc);
    uint32_t ofsMclq = 0, sizeLiquid = 0;
    if (!mclq.empty()) {
        ofsMclq    = putChunk(w, "MCLQ", mclq);
        sizeLiquid = static_cast<uint32_t>(mclq.size()) + 8u;
    }

    // MCSE: 52-byte vanilla sound-emitter records. The parser keeps the ids,
    // position and distances but drops the 20-byte timing/count tail, so the
    // tail re-serializes zeroed (the parsed model is the source of truth).
    uint32_t ofsMcse = 0;
    if (!mc.soundEmitters.empty()) {
        ByteWriter v;
        for (const SoundEmitter& e : mc.soundEmitters) {
            v.u32(e.soundPointID);
            v.u32(e.soundNameID);
            v.f32(e.position.x); v.f32(e.position.y); v.f32(e.position.z);
            v.f32(e.minDistance);
            v.f32(e.maxDistance);
            v.f32(e.cutoffDistance);
            for (int i = 0; i < 20; ++i) v.u8(0);   // timing/count tail
        }
        ofsMcse = putChunk(w, "MCSE", v.take());
    }

    std::vector<uint8_t> body = w.take();

    // Header flags: refresh the data-derived bits (shadow present, liquid
    // layer set) and preserve everything else (impassable, do-not-fix-alpha).
    uint32_t flags = mc.flags;
    flags &= ~(MCNK_HAS_MCSH | MCNK_LQ_RIVER | MCNK_LQ_OCEAN | MCNK_LQ_MAGMA | MCNK_LQ_SLIME);
    flags |= mcnkLiquidFlags(mc);
    if (!mc.shadow.empty()) flags |= MCNK_HAS_MCSH;

    poke32(body, kOffFlags,        flags);
    poke32(body, kOffIndexX,       mc.indexX);
    poke32(body, kOffIndexY,       mc.indexY);
    poke32(body, kOffNLayers,      static_cast<uint32_t>(mc.layers.size()));
    poke32(body, kOffNDoodadRefs,  static_cast<uint32_t>(mc.doodadRefs.size()));
    poke32(body, kOffOfsMCVT,      ofsMcvt);
    poke32(body, kOffOfsMCNR,      ofsMcnr);
    poke32(body, kOffOfsMCLY,      ofsMcly);
    poke32(body, kOffOfsMCRF,      ofsMcrf);
    poke32(body, kOffOfsMCAL,      ofsMcal);
    poke32(body, kOffSizeMCAL,     sizeMcal);
    poke32(body, kOffOfsMCSH,      ofsMcsh);
    poke32(body, kOffSizeMCSH,     sizeMcsh);
    poke32(body, kOffAreaId,       mc.areaId);
    poke32(body, kOffNMapObjRefs,  static_cast<uint32_t>(mc.wmoRefs.size()));
    poke16(body, kOffHoles,        mc.holes);
    for (size_t i = 0; i < mc.predTex.size(); ++i)
        body[kOffPredTex + i] = mc.predTex[i];
    for (size_t i = 0; i < mc.noEffectDoodad.size(); ++i)
        body[kOffNoEffectDoodad + i] = mc.noEffectDoodad[i];
    poke32(body, kOffNSndEmitters, static_cast<uint32_t>(mc.soundEmitters.size()));
    poke32(body, kOffOfsMCSE,      ofsMcse);
    poke32(body, kOffOfsMCLQ,      ofsMclq);
    poke32(body, kOffSizeLiquid,   sizeLiquid);
    pokeF32(body, kOffPosition + 0, mc.position.x);
    pokeF32(body, kOffPosition + 4, mc.position.y);
    pokeF32(body, kOffPosition + 8, mc.position.z);

    return body;
}

} // namespace

std::vector<uint8_t> writeAdtFull(const Adt& adt, const std::vector<MapChunk>& chunks) {
    // Rebuild the model name tables from the live placement lists, assigning
    // each record a fresh MMID/MWID index (stale input indices are ignored).
    NameTable m2Names, wmoNames;
    std::vector<uint32_t> mmidIdx, mwidIdx;
    mmidIdx.reserve(adt.doodads.size());
    mwidIdx.reserve(adt.wmos.size());
    for (const DoodadDef& d : adt.doodads) mmidIdx.push_back(m2Names.add(d.modelName));
    for (const WmoDef& mo : adt.wmos)      mwidIdx.push_back(wmoNames.add(mo.modelName));

    ByteWriter w;

    // MVER: version 18.
    {
        ByteWriter v;
        v.u32(18);
        putChunk(w, "MVER", v.take());
    }

    // MHDR + MCIN placeholders; both are fixed up LAST, once every offset is
    // known. MHDR offsets are relative to its own data start.
    const size_t mhdrData = w.size() + 8;
    putChunk(w, "MHDR", std::vector<uint8_t>(kMhdrSize, 0));
    const size_t mcinPos = putChunk(w, "MCIN", std::vector<uint8_t>(kMcinEntries * 16, 0));

    // MTEX: zero-terminated texture path table (MCLY.textureId indexes it).
    std::vector<uint8_t> mtex;
    for (const std::string& t : adt.textures) {
        mtex.insert(mtex.end(), t.begin(), t.end());
        mtex.push_back(0);
    }
    const size_t mtexPos = putChunk(w, "MTEX", mtex);

    const size_t mmdxPos = putChunk(w, "MMDX", m2Names.blobBytes());
    const size_t mmidPos = putChunk(w, "MMID", m2Names.offsetBytes());
    const size_t mwmoPos = putChunk(w, "MWMO", wmoNames.blobBytes());
    const size_t mwidPos = putChunk(w, "MWID", wmoNames.offsetBytes());

    // MDDF: 36 bytes per doodad placement.
    std::vector<uint8_t> mddf;
    {
        ByteWriter v;
        for (size_t i = 0; i < adt.doodads.size(); ++i) {
            const DoodadDef& d = adt.doodads[i];
            v.u32(mmidIdx[i]);
            v.u32(d.uniqueId);
            for (float p : d.pos) v.f32(p);
            for (float r : d.rot) v.f32(r);
            v.u16(d.scale);
            v.u16(d.flags);
        }
        mddf = v.take();
    }
    const size_t mddfPos = putChunk(w, "MDDF", mddf);

    // MODF: 64 bytes per map-object placement.
    std::vector<uint8_t> modf;
    {
        ByteWriter v;
        for (size_t i = 0; i < adt.wmos.size(); ++i) {
            const WmoDef& mo = adt.wmos[i];
            v.u32(mwidIdx[i]);
            v.u32(mo.uniqueId);
            for (float p : mo.pos)     v.f32(p);
            for (float r : mo.rot)     v.f32(r);
            for (float e : mo.extents) v.f32(e);
            v.u16(mo.flags);
            v.u16(mo.doodadSet);
            v.u16(mo.nameSet);
            v.u16(0);   // padding
        }
        modf = v.take();
    }
    const size_t modfPos = putChunk(w, "MODF", modf);

    // The 256 MCNKs (or however many the caller holds -- synthetic fixtures
    // are smaller; spare MCIN entries stay zeroed).
    struct McnkLoc { uint32_t ofs; uint32_t size; };
    std::vector<McnkLoc> locs;
    locs.reserve(chunks.size());
    for (const MapChunk& mc : chunks) {
        const std::vector<uint8_t> payload = buildMcnkPayload(mc);
        const uint32_t at = putChunk(w, "MCNK", payload);
        locs.push_back({ at, static_cast<uint32_t>(payload.size()) + 8u });
    }

    std::vector<uint8_t> out = w.take();

    // MHDR fix-up: u32 flags, then the chunk offsets (each relative to the
    // MHDR data start, pointing at the referenced chunk's magic), vanilla
    // field order mcin, mtex, mmdx, mmid, mwmo, mwid, mddf, modf; tail zeroed.
    auto rel = [&](size_t pos) { return static_cast<uint32_t>(pos - mhdrData); };
    poke32(out, mhdrData + 4 * 1, rel(mcinPos));
    poke32(out, mhdrData + 4 * 2, rel(mtexPos));
    poke32(out, mhdrData + 4 * 3, rel(mmdxPos));
    poke32(out, mhdrData + 4 * 4, rel(mmidPos));
    poke32(out, mhdrData + 4 * 5, rel(mwmoPos));
    poke32(out, mhdrData + 4 * 6, rel(mwidPos));
    poke32(out, mhdrData + 4 * 7, rel(mddfPos));
    poke32(out, mhdrData + 4 * 8, rel(modfPos));

    // MCIN fix-up: 256 x { u32 absolute offset of the MCNK's magic from the
    // file start, u32 size including the 8-byte chunk header, u32 flags = 0,
    // u32 pad = 0 }. Entries beyond the supplied chunks stay zeroed.
    const size_t mcinData = mcinPos + 8;
    const size_t n = std::min(locs.size(), kMcinEntries);
    for (size_t i = 0; i < n; ++i) {
        poke32(out, mcinData + i * 16 + 0, locs[i].ofs);
        poke32(out, mcinData + i * 16 + 4, locs[i].size);
    }
    return out;
}

void DirtyTiles::markForPlacement(uint32_t uniqueId, const TileRefsFn& tileRefs) {
    if (!tileRefs) return;
    for (const std::pair<int, int>& t : tileRefs(uniqueId)) mark(t.first, t.second);
}

bool shouldAutosave(const AutosavePolicy& policy, uint64_t nowMs,
                    uint64_t lastSaveMs, uint64_t lastInputMs,
                    uint64_t modCount, uint64_t lastModCount) {
    if (modCount == lastModCount) return false;              // nothing changed
    if (nowMs < lastInputMs || nowMs - lastInputMs <= policy.idleMs)
        return false;                                        // user mid-edit
    if (nowMs < lastSaveMs || nowMs - lastSaveMs < policy.intervalMs)
        return false;                                        // saved too recently
    return true;
}

std::filesystem::path nextBackupName(const std::filesystem::path& dir,
                                     const std::string& base, int maxBackups) {
    if (maxBackups < 1) maxBackups = 1;
    std::error_code ec;
    fs::create_directories(dir, ec);

    const std::string prefix = base + ".";
    const std::string suffix = ".adt";
    auto slotPath = [&](int n) { return dir / (prefix + std::to_string(n) + suffix); };

    // Collect the existing backup numbers "<base>.<N>.adt".
    std::vector<int> nums;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() <= prefix.size() + suffix.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string mid = name.substr(prefix.size(),
                                            name.size() - prefix.size() - suffix.size());
        if (mid.empty() || mid.size() > 9) continue;   // 9 digits: no int overflow
        bool digits = true;
        for (char c : mid)
            if (!std::isdigit(static_cast<unsigned char>(c))) { digits = false; break; }
        if (!digits) continue;
        nums.push_back(std::atoi(mid.c_str()));
    }
    std::sort(nums.begin(), nums.end());

    // Ring full: drop the oldest until one slot is free.
    while (static_cast<int>(nums.size()) >= maxBackups) {
        fs::remove(slotPath(nums.front()), ec);
        nums.erase(nums.begin());
    }
    // Renumber the survivors down to 1..k (ascending, so a rename's target
    // slot is always already vacated).
    for (size_t i = 0; i < nums.size(); ++i) {
        const int want = static_cast<int>(i) + 1;
        if (nums[i] != want) {
            fs::rename(slotPath(nums[i]), slotPath(want), ec);
            nums[i] = want;
        }
    }
    return slotPath(static_cast<int>(nums.size()) + 1);
}

} // namespace wf
