#include "test.hpp"
#include "adt_writer.hpp"
#include "asset_loader.hpp"
#include "client_data.hpp"
#include "liquid_edit.hpp"
#include "mpq.hpp"
#include "terrain.hpp"
#include "wow_files.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace wf;
namespace fs = std::filesystem;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void putf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(b,v); }
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p){
    // Chunk magics are stored reversed on disk; forEachChunk un-reverses.
    b.push_back(m[3]); b.push_back(m[2]); b.push_back(m[1]); b.push_back(m[0]);
    put32(b,(uint32_t)p.size()); b.insert(b.end(),p.begin(),p.end());
}

// Little-endian reads out of a serialized buffer (for the MHDR/MCIN checks).
uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[at + i]) << (8 * i);
    return v;
}
std::string magicAt(const std::vector<uint8_t>& b, size_t at) {
    // On-disk fourcc is reversed; un-reverse to the human-readable form.
    return std::string{ (char)b[at+3], (char)b[at+2], (char)b[at+1], (char)b[at] };
}

// A "rich" MCNK exercising every sub-chunk the writer emits: two MCLY layers
// + MCAL, MCRF refs, MCSH, one river MCLQ layer, one MCSE emitter, non-zero
// header fields. Real-vanilla shape: header offsets populated, the 13 MCNR
// pad bytes OUTSIDE the declared size.
std::vector<uint8_t> makeRichMcnk() {
    std::vector<uint8_t> hdr(128, 0);
    auto h32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    auto h16 = [&](size_t off, uint16_t v){ hdr[off]=v&0xFF; hdr[off+1]=(v>>8)&0xFF; };
    auto hf  = [&](size_t off, float f){ uint32_t v; std::memcpy(&v,&f,4); h32(off,v); };
    h32(0x00, MCNK_HAS_MCSH | MCNK_LQ_RIVER | MCNK_IMPASSABLE);
    h32(0x04, 3);          // IndexX
    h32(0x08, 5);          // IndexY
    h32(0x0C, 2);          // nLayers
    h32(0x10, 2);          // nDoodadRefs
    h32(0x34, 777);        // areaId
    h32(0x38, 1);          // nMapObjRefs
    h16(0x3C, 0x0021);     // holes
    for (int i = 0; i < 16; ++i) hdr[0x40 + i] = static_cast<uint8_t>(i);        // predTex
    for (int i = 0; i <  8; ++i) hdr[0x50 + i] = static_cast<uint8_t>(0xA0 + i); // noEffectDoodad
    h32(0x58, 1);          // nSndEmitters
    hf(0x68, 111.0f); hf(0x6C, 222.0f); hf(0x70, 333.0f);   // position

    std::vector<uint8_t> mcvt;
    for (int i = 0; i < 145; ++i) putf(mcvt, i * 0.5f);

    // Straight-up normals plus one tilted (near-unit int8 triples round-trip
    // through the quantizer).
    std::vector<uint8_t> mcnr;
    for (int i = 0; i < 145; ++i) {
        if (i == 7) { mcnr.push_back(50); mcnr.push_back(0); mcnr.push_back(117); }
        else        { mcnr.push_back(0);  mcnr.push_back(0); mcnr.push_back(127); }
    }

    std::vector<uint8_t> mcly;
    put32(mcly, 0); put32(mcly, 0);              // layer0: base, no alpha
    put32(mcly, 0); put32(mcly, 7);              // effectId 7
    put32(mcly, 1); put32(mcly, MCLY_USE_ALPHA); // layer1: alpha-mapped
    put32(mcly, 0); put32(mcly, 42);             // ofsAlpha 0, effectId 42

    std::vector<uint8_t> mcrf;
    put32(mcrf, 0); put32(mcrf, 1);              // doodad (MDDF) indices
    put32(mcrf, 0);                              // map-object (MODF) index

    std::vector<uint8_t> mcal(2048);
    for (size_t k = 0; k < mcal.size(); ++k) mcal[k] = static_cast<uint8_t>(k & 0xFF);

    // Shadow bits away from rows/cols 62-63 so the parse-time 63->64 edge
    // duplication cannot disturb the comparison.
    std::vector<uint8_t> mcsh(512, 0);
    auto setSh = [&](int row, int col) {
        size_t bit = static_cast<size_t>(row) * 64 + col;
        mcsh[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
    };
    setSh(10, 10); setSh(20, 33);

    // One river MCLQ block: min/max + 81 x {depth,0,0,0,height} + 64 tile
    // bytes + nFlowvs + two fixed SWFlowv records = 804 bytes.
    std::vector<uint8_t> mclq;
    putf(mclq, 10.0f); putf(mclq, 20.0f);
    for (int i = 0; i < 81; ++i) {
        mclq.push_back(static_cast<uint8_t>(i));   // depth ramp
        mclq.push_back(0); mclq.push_back(0); mclq.push_back(0);
        putf(mclq, 15.0f + i * 0.125f);
    }
    for (int i = 0; i < 64; ++i) mclq.push_back(i == 0 ? 0x0F : 0x04);  // tile 0 masked
    put32(mclq, 0);
    for (int i = 0; i < 2 * 40; ++i) mclq.push_back(0);

    // One 52-byte MCSE sound emitter (20-byte timing tail not retained).
    std::vector<uint8_t> mcse;
    put32(mcse, 555); put32(mcse, 777);
    putf(mcse, 10.0f); putf(mcse, 20.0f); putf(mcse, 30.0f);
    putf(mcse, 5.0f); putf(mcse, 45.0f); putf(mcse, 60.0f);
    for (int i = 0; i < 20; ++i) mcse.push_back(0);

    // Assemble body = header + sub-chunks, recording each sub-chunk's offset
    // (relative to the MCNK data start) in the header, like real tiles.
    std::vector<uint8_t> body = hdr;
    h32(0x14, (uint32_t)body.size()); chunk(body, "MCVT", mcvt);
    h32(0x18, (uint32_t)body.size()); chunk(body, "MCNR", mcnr);
    for (int i = 0; i < 13; ++i) body.push_back(0);   // MCNR pad OUTSIDE its size
    h32(0x1C, (uint32_t)body.size()); chunk(body, "MCLY", mcly);
    h32(0x20, (uint32_t)body.size()); chunk(body, "MCRF", mcrf);
    h32(0x24, (uint32_t)body.size());
    h32(0x28, (uint32_t)mcal.size());
    chunk(body, "MCAL", mcal);
    h32(0x2C, (uint32_t)body.size());
    h32(0x30, 512);
    chunk(body, "MCSH", mcsh);
    h32(0x60, (uint32_t)body.size());
    h32(0x64, (uint32_t)mclq.size() + 8);
    chunk(body, "MCLQ", mclq);
    h32(0x5C, (uint32_t)body.size()); chunk(body, "MCSE", mcse);
    std::memcpy(body.data(), hdr.data(), 128);        // finished header in

    std::vector<uint8_t> out;
    chunk(out, "MCNK", body);
    return out;
}

// A minimal MCNK: zero layers, no refs/shadow/liquid/emitters -- the empty
// edge cases of the writer.
std::vector<uint8_t> makeBareMcnk() {
    std::vector<uint8_t> hdr(128, 0);
    auto h32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    h32(0x04, 1);   // IndexX
    h32(0x08, 5);   // IndexY

    std::vector<uint8_t> mcvt;
    for (int i = 0; i < 145; ++i) putf(mcvt, 4.0f);
    std::vector<uint8_t> mcnr;
    for (int i = 0; i < 145; ++i) { mcnr.push_back(0); mcnr.push_back(0); mcnr.push_back(127); }

    std::vector<uint8_t> body = hdr;
    h32(0x14, (uint32_t)body.size()); chunk(body, "MCVT", mcvt);
    h32(0x18, (uint32_t)body.size()); chunk(body, "MCNR", mcnr);
    for (int i = 0; i < 13; ++i) body.push_back(0);
    std::memcpy(body.data(), hdr.data(), 128);

    std::vector<uint8_t> out;
    chunk(out, "MCNK", body);
    return out;
}

// A full synthetic tile: MTEX, MMDX/MMID, MWMO/MWID, MDDF (2), MODF (1) and
// two MCNKs (rich + bare). Built independently of the writer so identity
// bugs cannot cancel out.
std::vector<uint8_t> makeSyntheticAdt() {
    std::vector<uint8_t> adt;

    std::vector<uint8_t> mver; put32(mver, 18);
    chunk(adt, "MVER", mver);

    std::vector<uint8_t> mtex;
    for (const char* t : { "Tileset\\Generic\\base.blp", "Tileset\\Generic\\grass.blp" }) {
        for (const char* p = t; *p; ++p) mtex.push_back((uint8_t)*p);
        mtex.push_back(0);
    }
    chunk(adt, "MTEX", mtex);

    const std::string m2a = "World\\doodad\\tree.mdx";
    const std::string m2b = "World\\doodad\\rock.mdx";
    std::vector<uint8_t> mmdx, mmid;
    put32(mmid, 0);
    for (char c : m2a) mmdx.push_back((uint8_t)c); mmdx.push_back(0);
    put32(mmid, (uint32_t)mmdx.size());
    for (char c : m2b) mmdx.push_back((uint8_t)c); mmdx.push_back(0);
    chunk(adt, "MMDX", mmdx);
    chunk(adt, "MMID", mmid);

    const std::string wmoName = "World\\wmo\\house.wmo";
    std::vector<uint8_t> mwmo, mwid;
    put32(mwid, 0);
    for (char c : wmoName) mwmo.push_back((uint8_t)c); mwmo.push_back(0);
    chunk(adt, "MWMO", mwmo);
    chunk(adt, "MWID", mwid);

    std::vector<uint8_t> mddf;
    auto putDoodad = [&](uint32_t mmidIdx, uint32_t uid, float x, float y, float z) {
        put32(mddf, mmidIdx); put32(mddf, uid);
        putf(mddf, x); putf(mddf, y); putf(mddf, z);
        putf(mddf, 0.0f); putf(mddf, 90.0f); putf(mddf, 0.0f);
        put16(mddf, 2048); put16(mddf, 1);
    };
    putDoodad(0, 1001, 100.0f, 200.0f, 30.0f);
    putDoodad(1, 1002, 110.0f, 210.0f, 31.0f);
    chunk(adt, "MDDF", mddf);

    std::vector<uint8_t> modf;
    put32(modf, 0); put32(modf, 2001);
    putf(modf, 400.0f); putf(modf, 500.0f); putf(modf, 60.0f);
    putf(modf, 0.0f); putf(modf, 45.0f); putf(modf, 0.0f);
    for (int i = 0; i < 6; ++i) putf(modf, 10.0f * i);
    put16(modf, 0); put16(modf, 1); put16(modf, 2); put16(modf, 0);
    chunk(adt, "MODF", modf);

    std::vector<uint8_t> rich = makeRichMcnk();
    std::vector<uint8_t> bare = makeBareMcnk();
    adt.insert(adt.end(), rich.begin(), rich.end());
    adt.insert(adt.end(), bare.begin(), bare.end());
    return adt;
}

// Field-equality comparison of two parsed tiles (chunk order may legally
// differ across a re-serialization; here both sides emit in the same order,
// so index-wise comparison is enough).
void compareParsedTiles(const Adt& a, const std::vector<MapChunk>& ca,
                        const Adt& b, const std::vector<MapChunk>& cb,
                        bool bigAlpha) {
    CHECK(a.textures == b.textures);

    CHECK(a.doodads.size() == b.doodads.size());
    for (size_t i = 0; i < a.doodads.size() && i < b.doodads.size(); ++i) {
        const DoodadDef& x = a.doodads[i];
        const DoodadDef& y = b.doodads[i];
        CHECK(x.uniqueId == y.uniqueId);
        CHECK(x.modelName == y.modelName);
        CHECK(x.scale == y.scale && x.flags == y.flags);
        for (int k = 0; k < 3; ++k) { CHECK(x.pos[k] == y.pos[k]); CHECK(x.rot[k] == y.rot[k]); }
    }
    CHECK(a.wmos.size() == b.wmos.size());
    for (size_t i = 0; i < a.wmos.size() && i < b.wmos.size(); ++i) {
        const WmoDef& x = a.wmos[i];
        const WmoDef& y = b.wmos[i];
        CHECK(x.uniqueId == y.uniqueId);
        CHECK(x.modelName == y.modelName);
        CHECK(x.flags == y.flags && x.doodadSet == y.doodadSet && x.nameSet == y.nameSet);
        for (int k = 0; k < 3; ++k) { CHECK(x.pos[k] == y.pos[k]); CHECK(x.rot[k] == y.rot[k]); }
        for (int k = 0; k < 6; ++k) CHECK(x.extents[k] == y.extents[k]);
    }

    CHECK(ca.size() == cb.size());
    for (size_t c = 0; c < ca.size() && c < cb.size(); ++c) {
        const MapChunk& x = ca[c];
        const MapChunk& y = cb[c];
        CHECK(x.flags == y.flags);
        CHECK(x.indexX == y.indexX && x.indexY == y.indexY);
        CHECK(x.areaId == y.areaId);
        CHECK(x.holes == y.holes);
        CHECK(x.position.x == y.position.x && x.position.y == y.position.y &&
              x.position.z == y.position.z);
        CHECK(x.heights == y.heights);                       // float bits preserved
        bool normalsClose = true;                            // int8 quantization
        for (int k = 0; k < 145; ++k) {
            if (std::fabs(x.normals[k].x - y.normals[k].x) > 0.02f ||
                std::fabs(x.normals[k].y - y.normals[k].y) > 0.02f ||
                std::fabs(x.normals[k].z - y.normals[k].z) > 0.02f) normalsClose = false;
        }
        CHECK(normalsClose);
        CHECK(x.layers.size() == y.layers.size());
        for (size_t l = 0; l < x.layers.size() && l < y.layers.size(); ++l) {
            CHECK(x.layers[l].textureId == y.layers[l].textureId);
            CHECK(x.layers[l].flags     == y.layers[l].flags);
            CHECK(x.layers[l].ofsAlpha  == y.layers[l].ofsAlpha);
            CHECK(x.layers[l].effectId  == y.layers[l].effectId);
        }
        CHECK(x.alpha == y.alpha);
        for (size_t l = 1; l < x.layers.size() && l < y.layers.size(); ++l) {
            AlphaMap ma = decodeAlphaMap(x, l, bigAlpha);
            AlphaMap mb = decodeAlphaMap(y, l, bigAlpha);
            CHECK(ma.texels == mb.texels);
        }
        CHECK(x.shadow == y.shadow);
        CHECK(x.doodadRefs == y.doodadRefs);
        CHECK(x.wmoRefs == y.wmoRefs);
        CHECK(x.predTex == y.predTex);
        CHECK(x.noEffectDoodad == y.noEffectDoodad);
        CHECK(x.soundEmitters.size() == y.soundEmitters.size());
        for (size_t e = 0; e < x.soundEmitters.size() && e < y.soundEmitters.size(); ++e) {
            const SoundEmitter& p = x.soundEmitters[e];
            const SoundEmitter& q = y.soundEmitters[e];
            CHECK(p.soundPointID == q.soundPointID && p.soundNameID == q.soundNameID);
            CHECK(p.position.x == q.position.x && p.position.y == q.position.y &&
                  p.position.z == q.position.z);
            CHECK(p.minDistance == q.minDistance && p.maxDistance == q.maxDistance &&
                  p.cutoffDistance == q.cutoffDistance);
        }
        CHECK(x.hasLiquid == y.hasLiquid);
        CHECK(x.liquidLayers.size() == y.liquidLayers.size());
        for (size_t l = 0; l < x.liquidLayers.size() && l < y.liquidLayers.size(); ++l) {
            const MclqLayer& p = x.liquidLayers[l];
            const MclqLayer& q = y.liquidLayers[l];
            CHECK(p.type == q.type);
            CHECK(p.minHeight == q.minHeight && p.maxHeight == q.maxHeight);
            CHECK(p.heights == q.heights);
            CHECK(p.depth == q.depth);
            CHECK(p.renderFlags == q.renderFlags);
        }
    }
}

// MHDR + MCIN self-consistency: every offset must land on the chunk it
// promises. MHDR offsets are relative to its data start; MCIN offsets are
// absolute from the file start.
void checkHeaderConsistency(const std::vector<uint8_t>& out, size_t nChunks) {
    // MVER is 12 bytes, so MHDR's magic sits at 12, its data at 20.
    CHECK(magicAt(out, 0) == "MVER");
    CHECK(magicAt(out, 12) == "MHDR");
    const size_t mhdrData = 20;
    CHECK(rd32(out, 12 + 4) == 64);                 // vanilla MHDR is 64 bytes

    const char* order[] = { "MCIN", "MTEX", "MMDX", "MMID", "MWMO", "MWID", "MDDF", "MODF" };
    for (int f = 0; f < 8; ++f) {
        const uint32_t ofs = rd32(out, mhdrData + 4 * (static_cast<size_t>(f) + 1));
        CHECK(magicAt(out, mhdrData + ofs) == order[f]);
    }

    const size_t mcinPos  = mhdrData + rd32(out, mhdrData + 4);
    CHECK(rd32(out, mcinPos + 4) == 256u * 16u);    // 256 entries x 16 bytes
    const size_t mcinData = mcinPos + 8;
    for (size_t i = 0; i < 256; ++i) {
        const uint32_t ofs = rd32(out, mcinData + i * 16 + 0);
        const uint32_t sz  = rd32(out, mcinData + i * 16 + 4);
        if (i < nChunks) {
            CHECK(magicAt(out, ofs) == "MCNK");
            CHECK(rd32(out, ofs + 4) + 8 == sz);    // size spans magic+size+payload
            CHECK(static_cast<size_t>(ofs) + sz <= out.size());
        } else {
            CHECK(ofs == 0 && sz == 0);             // spare entries stay zeroed
        }
    }
}

bool writeFileBytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f.good();
}
std::string readFileText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
} // namespace

void test_adt_writer() {
    std::printf("[adt_writer]\n");

    const std::vector<uint8_t> src = makeSyntheticAdt();

    // --- (1) IDENTITY round-trip: parse -> writeAdtFull -> re-parse ---------
    {
        ParsedTileState st{ parseAdt(src), parseChunks(src) };
        CHECK(st.chunks.size() == 2);
        CHECK(st.adt.doodads.size() == 2 && st.adt.wmos.size() == 1);

        const std::vector<uint8_t> out = writeAdtFull(st);
        Adt a2;
        std::vector<MapChunk> c2;
        bool parsed = true;
        try {
            a2 = parseAdt(out);
            c2 = parseChunks(out);
        } catch (...) { parsed = false; }
        CHECK(parsed);
        compareParsedTiles(st.adt, st.chunks, a2, c2, /*bigAlpha*/false);
        checkHeaderConsistency(out, st.chunks.size());

        // Spot-checks that the fixture's substance really made it through.
        CHECK(c2.size() == 2);
        CHECK(c2[0].areaId == 777 && c2[0].holes == 0x0021);
        CHECK(c2[0].flags & MCNK_IMPASSABLE);
        CHECK(c2[0].layers.size() == 2 && c2[0].alpha.size() == 2048);
        CHECK(c2[0].shadow.size() == 512 && shadowAt(c2[0], 10, 10));
        CHECK(c2[0].hasLiquid && c2[0].liquidType == LiquidType::River);
        CHECK(c2[0].soundEmitters.size() == 1 && c2[0].soundEmitters[0].soundPointID == 555);
        CHECK(c2[1].layers.empty() && !c2[1].hasLiquid && c2[1].shadow.empty());
        CHECK(a2.doodads[1].modelName == "World\\doodad\\rock.mdx");
        CHECK(a2.wmos[0].modelName == "World\\wmo\\house.wmo");

        // The writer's output re-serializes to the identical byte stream --
        // writeAdtFull is a fixed point of parse+write.
        ParsedTileState st2{ a2, c2 };
        CHECK(writeAdtFull(st2) == out);
    }

    // --- (2) GROWTH: add layers, doodads and liquid, then re-serialize ------
    {
        ParsedTileState st{ parseAdt(src), parseChunks(src) };

        // Two more MTEX entries + grow chunk 0 to the 4-layer vanilla cap;
        // packAlphaLayers rebuilds MCAL (2048 B per overlay layer) + offsets.
        st.adt.textures.push_back("Tileset\\Generic\\snow.blp");
        st.adt.textures.push_back("Tileset\\Generic\\rock.blp");
        MapChunk& mc0 = st.chunks[0];
        for (uint32_t tex : { 2u, 3u }) {
            TexLayer l;
            l.textureId = tex;
            l.flags     = MCLY_USE_ALPHA;
            mc0.layers.push_back(l);
        }
        std::vector<AlphaMap> maps(4);
        maps[1].texels.fill(255);
        maps[2].texels.fill(17 * 5);
        for (int k = 0; k < 64 * 64; ++k)
            maps[3].texels[k] = static_cast<uint8_t>((k % 16) * 17);
        packAlphaLayers(mc0, maps, /*bigAlpha*/false);

        // Three new doodads with a model name the tile has never seen (grows
        // MMDX/MMID) plus one reusing an existing name (dedup, first use).
        for (int i = 0; i < 3; ++i) {
            DoodadDef d;
            d.uniqueId  = 5000 + static_cast<uint32_t>(i);
            d.modelName = "World\\doodad\\bush.mdx";
            d.pos[0] = 10.0f * i; d.pos[1] = 1.0f; d.pos[2] = 2.0f;
            st.adt.doodads.push_back(d);
        }
        DoodadDef dup;
        dup.uniqueId  = 5100;
        dup.modelName = "World\\doodad\\tree.mdx";
        st.adt.doodads.push_back(dup);

        // A liquid layer on the previously dry chunk 1, and a shadow map on a
        // chunk that had no MCSH in the file (the writeAdtShadows limitation
        // this writer exists to lift).
        setLiquidCells(st.chunks[1], LiquidType::River, ~0ull, 12.5f);
        st.chunks[1].shadow.assign(512, 0);
        st.chunks[1].shadow[(10 * 64 + 10) / 8] |= 1u << ((10 * 64 + 10) % 8);

        const std::vector<uint8_t> out = writeAdtFull(st);
        Adt a2;
        std::vector<MapChunk> c2;
        bool parsed = true;
        try {
            a2 = parseAdt(out);
            c2 = parseChunks(out);
        } catch (...) { parsed = false; }
        CHECK(parsed);
        checkHeaderConsistency(out, st.chunks.size());

        CHECK(a2.textures.size() == 4);
        CHECK(c2[0].layers.size() == 4);
        CHECK(c2[0].alpha.size() == 3u * 2048u);
        CHECK(c2[0].layers[3].textureId == 3);
        CHECK(decodeAlphaMap(c2[0], 1, false).at(20, 20) == 255);
        CHECK(decodeAlphaMap(c2[0], 2, false).at(0, 0) == 17 * 5);
        CHECK(decodeAlphaMap(c2[0], 3, false).at(0, 15) == 15 * 17);

        CHECK(a2.doodads.size() == 6);
        CHECK(a2.doodads[2].modelName == "World\\doodad\\bush.mdx");
        CHECK(a2.doodads[4].uniqueId == 5002);
        CHECK(a2.doodads[5].modelName == "World\\doodad\\tree.mdx");
        // Dedup: the reused name resolves through the SAME MMID slot as the
        // first-use doodad, and only one new name was appended.
        CHECK(a2.doodads[5].mmidIndex == a2.doodads[0].mmidIndex);
        CHECK(a2.m2Offsets.size() == 3);

        CHECK(c2[1].hasLiquid && c2[1].liquidType == LiquidType::River);
        CHECK(c2[1].liquidLayers.size() == 1);
        CHECK_APPROX(c2[1].liquidLayers[0].heights[40], 12.5f);
        CHECK(c2[1].flags & MCNK_LQ_RIVER);
        CHECK(c2[1].shadow.size() == 512);
        CHECK(shadowAt(c2[1], 10, 10));
        CHECK(c2[1].flags & MCNK_HAS_MCSH);

        // Everything untouched by the edits still round-trips.
        CHECK(c2[0].areaId == 777);
        CHECK(c2[0].heights == st.chunks[0].heights);
    }

    // --- (3) project overlay: loose files shadow the MPQ chain --------------
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) / "wforge_adtw_ovr";
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);

        // Pure path mapping: '\' -> '/'.
        CHECK(overlayFilePath("proj", "World\\Maps\\T\\T_1_1.adt").generic_string() ==
              fs::path("proj").generic_string() + "/World/Maps/T/T_1_1.adt");

        const std::string tilePath  = "World\\Maps\\T\\T_1_1.adt";
        const std::string otherPath = "World\\Maps\\T\\T_2_2.adt";
        const std::vector<uint8_t> otherBytes = { 1, 2, 3, 4 };
        CHECK(writeMpqArchive((root / "base.mpq").string(),
                              { { tilePath, src }, { otherPath, otherBytes } }));

        MpqManager mpq;
        CHECK(mpq.addArchive((root / "base.mpq").string()));
        AssetLoader loader(mpq);

        // No overlay set: reads come from the archive; saves are refused.
        std::vector<uint8_t> buf;
        CHECK(loader.readFileOverlaid(tilePath, buf));
        CHECK(buf == src);
        ParsedTileState st{ parseAdt(src), parseChunks(src) };
        CHECK(!loader.saveTile("T", 1, 1, st));

        // Overlay set: an edited save shadows the archived tile...
        loader.setOverlayDir(root / "project");
        st.chunks[0].areaId = 4242;
        CHECK(loader.saveTile("T", 1, 1, st));
        CHECK(loader.readFileOverlaid(tilePath, buf));
        CHECK(buf == writeAdtFull(st));
        CHECK(parseChunks(buf)[0].areaId == 4242);
        // ... the loader's ADT path sees the edit ...
        Adt adt;
        std::vector<MapChunk> chunks;
        CHECK(loader.loadAdt("T", 1, 1, adt, chunks));
        CHECK(chunks[0].areaId == 4242);
        // ... and an unrelated path still comes from the base source.
        CHECK(loader.readFileOverlaid(otherPath, buf));
        CHECK(buf == otherBytes);
        // The MPQ itself was never touched: dropping the overlay restores it.
        loader.setOverlayDir({});
        CHECK(loader.readFileOverlaid(tilePath, buf));
        CHECK(buf == src);
        loader.setOverlayDir(root / "project");

        // saveTileScene (save-current) + saveDirtyTiles (save-changed).
        TileScene ts;
        ts.sourceAdt    = st.adt;
        ts.sourceChunks = st.chunks;
        ts.sourceChunks[0].areaId = 4300;
        ts.sourceMap = "T";
        ts.sourceX = 1; ts.sourceY = 1;
        ts.hasSource = true;
        CHECK(loader.saveTileScene(ts));
        CHECK(loader.readFileOverlaid(tilePath, buf));
        CHECK(parseChunks(buf)[0].areaId == 4300);
        CHECK(!loader.saveTileScene(TileScene{}));   // no source -> refused

        DirtyTiles dirty;
        dirty.mark(1, 1);
        dirty.mark(9, 9);                            // not loaded -> stays marked
        st.chunks[0].areaId = 4400;
        int saved = loader.saveDirtyTiles("T", dirty,
            [&](int x, int y, ParsedTileState& out) {
                if (x != 1 || y != 1) return false;
                out = st;
                return true;
            });
        CHECK(saved == 1);
        CHECK(!dirty.contains(1, 1));                // saved: mark cleared
        CHECK(dirty.contains(9, 9));                 // unsaved: still dirty
        CHECK(loader.readFileOverlaid(tilePath, buf));
        CHECK(parseChunks(buf)[0].areaId == 4400);

        // save-all writes every provided tile regardless of dirt.
        st.chunks[0].areaId = 4500;
        CHECK(loader.saveAllTiles("T", { { 1, 1 } },
            [&](int, int, ParsedTileState& out) { out = st; return true; }) == 1);
        CHECK(loader.readFileOverlaid(tilePath, buf));
        CHECK(parseChunks(buf)[0].areaId == 4500);

        fs::remove_all(root, ec);
    }

    // --- (4) dirty tracking: border-straddling placements --------------------
    {
        DirtyTiles d;
        CHECK(d.empty());
        d.mark(3, 4);
        d.mark(3, 4);                                // set semantics: no dup
        CHECK(d.tiles.size() == 1 && d.contains(3, 4));

        // uniqueId 42 straddles two tiles: BOTH must go dirty on a move.
        auto refs = [](uint32_t uid) -> std::vector<std::pair<int, int>> {
            if (uid == 42) return { { 1, 1 }, { 1, 2 } };
            return {};
        };
        d.markForPlacement(42, refs);
        CHECK(d.contains(1, 1) && d.contains(1, 2));
        d.markForPlacement(7, refs);                 // unknown id -> no change
        CHECK(d.tiles.size() == 3);
        d.markForPlacement(42, nullptr);             // null callback -> no-op
        CHECK(d.tiles.size() == 3);
        d.clear();
        CHECK(d.empty());
    }

    // --- (5) autosave policy truth table + rolling backups -------------------
    {
        const AutosavePolicy pol;                    // 600000 ms / idle 2000 / 50
        const uint64_t now = 1000000;
        // All three conditions met.
        CHECK(shouldAutosave(pol, now, 0, now - 10000, 5, 4));
        // Nothing changed since the last save.
        CHECK(!shouldAutosave(pol, now, 0, now - 10000, 5, 5));
        // User still editing (idle must be STRICTLY greater than 2 s).
        CHECK(!shouldAutosave(pol, now, 0, now - 1500, 5, 4));
        CHECK(!shouldAutosave(pol, now, 0, now - 2000, 5, 4));
        CHECK(shouldAutosave(pol, now, 0, now - 2001, 5, 4));
        // Saved too recently (interval is inclusive at exactly intervalMs).
        CHECK(!shouldAutosave(pol, now, now - 599999, now - 10000, 5, 4));
        CHECK(shouldAutosave(pol, now, now - 600000, now - 10000, 5, 4));
        // Timestamps from the future never fire.
        CHECK(!shouldAutosave(pol, now, now + 1, now - 10000, 5, 4));
        CHECK(!shouldAutosave(pol, now, 0, now + 1, 5, 4));

        // Rolling backups: base.1.adt oldest .. base.N.adt newest; past
        // maxBackups the oldest is dropped and the survivors renumbered.
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "wforge_adtw_bak";
        fs::remove_all(dir, ec);
        const std::string base = "T_1_1";
        auto slot = [&](int n) { return dir / (base + "." + std::to_string(n) + ".adt"); };

        CHECK(nextBackupName(dir, base, 3) == slot(1));
        CHECK(writeFileBytes(slot(1), { 'A' }));
        CHECK(nextBackupName(dir, base, 3) == slot(2));
        CHECK(writeFileBytes(slot(2), { 'B' }));
        // Unrelated files must not confuse the numbering.
        CHECK(writeFileBytes(dir / (base + ".adt"), { 'X' }));
        CHECK(writeFileBytes(dir / "other.7.adt", { 'Y' }));
        CHECK(nextBackupName(dir, base, 3) == slot(3));
        CHECK(writeFileBytes(slot(3), { 'C' }));

        // Ring full: 'A' (oldest) is dropped, B/C shift down, slot 3 frees up.
        CHECK(nextBackupName(dir, base, 3) == slot(3));
        CHECK(readFileText(slot(1)) == "B");
        CHECK(readFileText(slot(2)) == "C");
        CHECK(!fs::exists(slot(3), ec));
        CHECK(writeFileBytes(slot(3), { 'D' }));

        // And again: steady state keeps exactly maxBackups slots.
        CHECK(nextBackupName(dir, base, 3) == slot(3));
        CHECK(readFileText(slot(1)) == "C");
        CHECK(readFileText(slot(2)) == "D");
        CHECK(readFileText(dir / (base + ".adt")) == "X");   // untouched
        CHECK(readFileText(dir / "other.7.adt") == "Y");
        fs::remove_all(dir, ec);
    }
}
