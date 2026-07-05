#include "test.hpp"
#include "asset_loader.hpp"
#include "asset_catalog.hpp"
#include "dbc_defs.hpp"      // DbcBuilder (display-DBC resolver test)
#include "mpq.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"
#include "coords.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace wf;

namespace {
void p8 (std::vector<uint8_t>& b, uint8_t v){ b.push_back(v); }
void p16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void p32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void pf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); p32(b,v); }
void praw(std::vector<uint8_t>& b, const char* s){ for(int i=0;i<4;i++) b.push_back((uint8_t)s[i]); }
void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v){ for(int i=0;i<4;i++) b[at+i]=(v>>(8*i))&0xFF; }
void align4(std::vector<uint8_t>& b){ while (b.size() % 4) b.push_back(0); }

// A minimal vanilla M2 (MD20 v0x100): one textured triangle whose texture
// record points at `texName`. Mirrors tests/test_m2.cpp's synthetic builder.
std::vector<uint8_t> makeM2(const std::string& texName) {
    std::vector<uint8_t> f(0x150, 0);
    f[0]='M'; f[1]='D'; f[2]='2'; f[3]='0';
    patch32(f, 0x004, 0x100);

    uint32_t nameOff = (uint32_t)f.size();
    for (char c : std::string("Doodad")) f.push_back((uint8_t)c);
    f.push_back(0); align4(f);

    uint32_t vtxOff = (uint32_t)f.size();
    for (int i = 0; i < 3; ++i) {
        pf(f,(float)i); pf(f,(float)(i*2)); pf(f,(float)(i*3));   // pos
        for (int k=0;k<4;k++) f.push_back(k==0?255:0);           // bone weights
        for (int k=0;k<4;k++) f.push_back(0);                     // bone indices
        pf(f,0.f); pf(f,0.f); pf(f,1.f);                          // normal
        pf(f,i*0.1f); pf(f,i*0.2f);                               // uv
        pf(f,0.f); pf(f,0.f);                                     // uv set 2
    }

    uint32_t texNameOff = (uint32_t)f.size();
    for (char c : texName) f.push_back((uint8_t)c);
    f.push_back(0); align4(f);
    uint32_t texOff = (uint32_t)f.size();
    p32(f,0); p32(f,0); p32(f,(uint32_t)texName.size()+1); p32(f,texNameOff);

    uint32_t lookupOff = (uint32_t)f.size();
    p16(f,0); p16(f,1); p16(f,2);
    uint32_t triOff = (uint32_t)f.size();
    p16(f,0); p16(f,1); p16(f,2);
    uint32_t subOff = (uint32_t)f.size();
    p16(f,0); p16(f,0); p16(f,0); p16(f,3); p16(f,0); p16(f,3);
    for (int i=0;i<20;i++) f.push_back(0);

    uint32_t viewOff = (uint32_t)f.size();
    p32(f,3); p32(f,lookupOff);
    p32(f,3); p32(f,triOff);
    p32(f,0); p32(f,0);
    p32(f,1); p32(f,subOff);
    p32(f,0); p32(f,0);
    p32(f,0);

    patch32(f, 0x008, 7);  patch32(f, 0x00C, nameOff);
    patch32(f, 0x044, 3);  patch32(f, 0x048, vtxOff);
    patch32(f, 0x04C, 1);  patch32(f, 0x050, viewOff);
    patch32(f, 0x05C, 1);  patch32(f, 0x060, texOff);
    return f;
}
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p){
    // ADT chunk magics are stored reversed on disk; forEachChunk un-reverses.
    b.push_back(m[3]); b.push_back(m[2]); b.push_back(m[1]); b.push_back(m[0]);
    p32(b, (uint32_t)p.size()); b.insert(b.end(), p.begin(), p.end());
}

// A minimal raw (BGRA8888) BLP2 with a single mip.
std::vector<uint8_t> makeRawBlp(int w, int h, Rgba color) {
    std::vector<uint8_t> b;
    praw(b, "BLP2"); p32(b, 1);            // type
    p8(b,3); p8(b,8); p8(b,0); p8(b,0);    // compression=raw, alphaDepth, enc, mips
    p32(b, (uint32_t)w); p32(b, (uint32_t)h);
    for (int i=0;i<16;i++) p32(b, i==0 ? 0x94u : 0u);   // mipOffsets
    for (int i=0;i<16;i++) p32(b, i==0 ? (uint32_t)(w*h*4) : 0u); // mipSizes
    for (int i=0;i<w*h;i++) { p8(b,color.b); p8(b,color.g); p8(b,color.r); p8(b,color.a); }
    return b;
}

// One base-layer MCNK referencing MTEX texture 0.
std::vector<uint8_t> makeMcnk() {
    std::vector<uint8_t> hdr(128, 0);
    auto w32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    w32(0x0C, 1);                          // nLayers = 1
    std::vector<uint8_t> mcvt; for (int i=0;i<145;i++) pf(mcvt, 0.0f);
    std::vector<uint8_t> mcnr;
    for (int i=0;i<145;i++){ mcnr.push_back(0); mcnr.push_back(0); mcnr.push_back(127); }
    for (int i=0;i<13;i++) mcnr.push_back(0);
    std::vector<uint8_t> mcly;
    p32(mcly, 0);  p32(mcly, 0); p32(mcly, 0); p32(mcly, 0);   // textureId 0, flags 0, ofs 0, effect 0
    std::vector<uint8_t> body = hdr;
    chunk(body, "MCVT", mcvt);
    chunk(body, "MCNR", mcnr);
    chunk(body, "MCLY", mcly);
    std::vector<uint8_t> out; chunk(out, "MCNK", body); return out;
}

// A minimal WMO root (one group) + its group file, for buildTileScene's WMO
// loading. Chunk magics are stored reversed on disk (chunk() reverses them).
std::vector<uint8_t> makeWmoRoot() {
    std::vector<uint8_t> root, mohd;
    p32(mohd,1); p32(mohd,1); p32(mohd,0); p32(mohd,0);   // nTex,nGroups,nPortals,nLights
    p32(mohd,0); p32(mohd,0); p32(mohd,0);                // doodadNames/Defs/Sets
    p32(mohd,0); p32(mohd,1);                             // ambColor, wmoID
    pf(mohd,0);pf(mohd,0);pf(mohd,0); pf(mohd,4);pf(mohd,4);pf(mohd,1);   // bbox
    p16(mohd,0); p16(mohd,0);
    chunk(root,"MOHD",mohd);
    // One texture + one material referencing it, so a render part resolves to it.
    std::vector<uint8_t> motx; for (char c : std::string("test.blp")) motx.push_back((uint8_t)c);
    motx.push_back(0); while (motx.size()%4) motx.push_back(0);
    chunk(root,"MOTX",motx);
    std::vector<uint8_t> momt;
    p32(momt,0); p32(momt,0); p32(momt,0); p32(momt,0);   // flags,shader,blend,diffuseOffset=0
    for (int i=0;i<12;i++) p32(momt,0);
    chunk(root,"MOMT",momt);
    std::vector<uint8_t> mogn; mogn.push_back(0); mogn.push_back(0);
    chunk(root,"MOGN",mogn);
    std::vector<uint8_t> mogi;
    p32(mogi,0x8); pf(mogi,0);pf(mogi,0);pf(mogi,0); pf(mogi,4);pf(mogi,4);pf(mogi,1); p32(mogi,0);
    chunk(root,"MOGI",mogi);
    return root;
}
std::vector<uint8_t> makeWmoGroup() {
    std::vector<uint8_t> movt;
    pf(movt,0);pf(movt,0);pf(movt,0); pf(movt,4);pf(movt,0);pf(movt,0); pf(movt,0);pf(movt,4);pf(movt,0);
    std::vector<uint8_t> monr; for(int i=0;i<3;i++){ pf(monr,0);pf(monr,0);pf(monr,1); }
    std::vector<uint8_t> motv; for(int i=0;i<3;i++){ pf(motv,0);pf(motv,0); }
    std::vector<uint8_t> movi; p16(movi,0); p16(movi,1); p16(movi,2);
    std::vector<uint8_t> mopy; mopy.push_back(0); mopy.push_back(0);
    std::vector<uint8_t> mogp(0x44, 0);
    { uint32_t fl=0x8; for(int i=0;i<4;i++) mogp[0x08+i]=(fl>>(8*i))&0xFF; }
    chunk(mogp,"MOVT",movt); chunk(mogp,"MONR",monr); chunk(mogp,"MOTV",motv);
    chunk(mogp,"MOVI",movi); chunk(mogp,"MOPY",mopy);
    std::vector<uint8_t> group; chunk(group,"MOGP",mogp);
    return group;
}

// MODF entry (64 bytes): mwidIndex, uniqueId, pos[3], rot[3], extents[6], flags,
// doodadSet, nameSet, pad.
void modfEntry(std::vector<uint8_t>& b, uint32_t mwid, uint32_t uid) {
    p32(b, mwid); p32(b, uid);
    pf(b,100);pf(b,200);pf(b,50);          // pos
    pf(b,0);pf(b,0);pf(b,0);               // rot
    for (int i=0;i<6;i++) pf(b,0);         // extents
    p16(b,0); p16(b,0); p16(b,0); p16(b,0);
}

// MDDF entry (36 bytes): mmidIndex, uniqueId, pos[3], rot[3], scale16, flags16.
void mddfEntry(std::vector<uint8_t>& b, uint32_t mmid, uint32_t uid,
               float px, float py, float pz, uint16_t scale) {
    p32(b, mmid); p32(b, uid);
    pf(b, px); pf(b, py); pf(b, pz);
    pf(b, 0.f); pf(b, 0.f); pf(b, 0.f);
    p16(b, scale); p16(b, 0);
}

std::vector<uint8_t> makeAdt() {
    std::vector<uint8_t> mtex; for (char c : std::string("test.blp")) mtex.push_back((uint8_t)c); mtex.push_back(0);

    // Two model names: one present in the archive, one deliberately missing.
    std::string names = std::string("doodad.m2") + '\0' + "missing.m2" + '\0';
    std::vector<uint8_t> mmdx(names.begin(), names.end());
    std::vector<uint8_t> mmid; p32(mmid, 0); p32(mmid, 10);   // offsets into MMDX

    std::vector<uint8_t> mddf;
    mddfEntry(mddf, 0, 1001, 100.f, 200.f, 300.f, 1024);      // -> doodad.m2 (present)
    mddfEntry(mddf, 1, 1002, 10.f,  20.f,  30.f,  1024);      // -> missing.m2 (skipped)

    // One WMO placement referencing "wmo\Box.wmo".
    std::string wmoName = std::string("wmo\\Box.wmo");
    std::vector<uint8_t> mwmo(wmoName.begin(), wmoName.end()); mwmo.push_back(0);
    std::vector<uint8_t> mwid; p32(mwid, 0);
    std::vector<uint8_t> modf; modfEntry(modf, 0, 2001);

    std::vector<uint8_t> adt;
    chunk(adt, "MTEX", mtex);
    chunk(adt, "MMDX", mmdx);
    chunk(adt, "MMID", mmid);
    chunk(adt, "MDDF", mddf);
    chunk(adt, "MWMO", mwmo);
    chunk(adt, "MWID", mwid);
    chunk(adt, "MODF", modf);
    std::vector<uint8_t> mcnk = makeMcnk();
    adt.insert(adt.end(), mcnk.begin(), mcnk.end());
    return adt;
}

std::vector<uint8_t> makeWdt(uint32_t mphdFlags = 0) {
    std::vector<uint8_t> mphd; p32(mphd, mphdFlags);    // bit 0x4 => 8-bit "big alpha"
    std::vector<uint8_t> main(64*64*8, 0);
    size_t e = (size_t)(32*64 + 32) * 8;                // tile (x=32,y=32)
    main[e] = 1;                                        // flags & 1 -> has ADT
    std::vector<uint8_t> wdt;
    chunk(wdt, "MPHD", mphd);
    chunk(wdt, "MAIN", main);
    return wdt;
}

// A 2-layer MCNK: base layer 0 + overlay layer 1 carrying an alpha map at
// MCAL offset 0. `mcal` is the raw alpha blob (4096 B for 8-bit, 2048 B for
// 4-bit) -- its interpretation is what the WDT big-alpha flag selects.
std::vector<uint8_t> makeMcnk2Layer(const std::vector<uint8_t>& mcal) {
    std::vector<uint8_t> hdr(128, 0);
    auto w32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    w32(0x0C, 2);                          // nLayers = 2
    std::vector<uint8_t> mcvt; for (int i=0;i<145;i++) pf(mcvt, 0.0f);
    std::vector<uint8_t> mcnr;
    for (int i=0;i<145;i++){ mcnr.push_back(0); mcnr.push_back(0); mcnr.push_back(127); }
    for (int i=0;i<13;i++) mcnr.push_back(0);
    std::vector<uint8_t> mcly;
    // layer 0: textureId 0, flags 0, ofsAlpha 0, effect 0
    p32(mcly, 0); p32(mcly, 0);            p32(mcly, 0); p32(mcly, 0);
    // layer 1: textureId 0, flags MCLY_USE_ALPHA(0x100), ofsAlpha 0, effect 0
    p32(mcly, 0); p32(mcly, MCLY_USE_ALPHA); p32(mcly, 0); p32(mcly, 0);
    std::vector<uint8_t> body = hdr;
    chunk(body, "MCVT", mcvt);
    chunk(body, "MCNR", mcnr);
    chunk(body, "MCLY", mcly);
    chunk(body, "MCAL", mcal);
    std::vector<uint8_t> out; chunk(out, "MCNK", body); return out;
}

// A base-layer MCNK flagged as river with one MCLQ liquid sub-chunk: min/max
// height envelope, 81 surface verts (height 5.0), and all 64 8x8 tiles set to
// "render" (flag 0x00). buildLiquidMesh should emit 64*2 triangles from this.
std::vector<uint8_t> makeMcnkLiquid() {
    std::vector<uint8_t> hdr(128, 0);
    auto w32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    w32(0x00, MCNK_LQ_RIVER);              // flags: river water
    w32(0x0C, 1);                          // nLayers = 1
    std::vector<uint8_t> mcvt; for (int i=0;i<145;i++) pf(mcvt, 0.0f);
    std::vector<uint8_t> mcnr;
    for (int i=0;i<145;i++){ mcnr.push_back(0); mcnr.push_back(0); mcnr.push_back(127); }
    for (int i=0;i<13;i++) mcnr.push_back(0);
    std::vector<uint8_t> mcly;
    p32(mcly, 0); p32(mcly, 0); p32(mcly, 0); p32(mcly, 0);
    std::vector<uint8_t> mclq;
    pf(mclq, 0.0f); pf(mclq, 10.0f);                       // min/max height
    for (int i=0;i<81;i++){ p32(mclq, 0); pf(mclq, 5.0f); } // union + height
    for (int i=0;i<64;i++) mclq.push_back(0x00);            // every tile renders
    std::vector<uint8_t> body = hdr;
    chunk(body, "MCVT", mcvt);
    chunk(body, "MCNR", mcnr);
    chunk(body, "MCLY", mcly);
    chunk(body, "MCLQ", mclq);
    std::vector<uint8_t> out; chunk(out, "MCNK", body); return out;
}

// Minimal ADT carrying MTEX + a single river-liquid MCNK (no placements).
std::vector<uint8_t> makeAdtLiquid() {
    std::vector<uint8_t> mtex; for (char c : std::string("test.blp")) mtex.push_back((uint8_t)c); mtex.push_back(0);
    std::vector<uint8_t> adt;
    chunk(adt, "MTEX", mtex);
    std::vector<uint8_t> mcnk = makeMcnkLiquid();
    adt.insert(adt.end(), mcnk.begin(), mcnk.end());
    return adt;
}

// Minimal ADT carrying just MTEX + a single 2-layer MCNK (no placements).
std::vector<uint8_t> makeAdt2Layer(const std::vector<uint8_t>& mcal) {
    std::vector<uint8_t> mtex; for (char c : std::string("test.blp")) mtex.push_back((uint8_t)c); mtex.push_back(0);
    std::vector<uint8_t> adt;
    chunk(adt, "MTEX", mtex);
    std::vector<uint8_t> mcnk = makeMcnk2Layer(mcal);
    adt.insert(adt.end(), mcnk.begin(), mcnk.end());
    return adt;
}
} // namespace

void test_asset() {
    std::printf("[asset]\n");

    // --- wmoInteriorTint: bounded ambient lift from MOLT interior lights ----
    {
        CHECK(wmoInteriorTint({}, 0.5f).x == 0.0f);    // no lights -> no boost
        WmoLight a; a.color = {1.0f, 0.5f, 0.25f}; a.intensity = 2.0f;
        Vec3 one = wmoInteriorTint({a}, 0.5f);         // single light -> colour * strength
        CHECK_APPROX(one.x, 0.5f);
        CHECK_APPROX(one.y, 0.25f);
        CHECK_APPROX(one.z, 0.125f);
        WmoLight b; b.color = {0.0f, 0.0f, 1.0f}; b.intensity = 6.0f;
        Vec3 mix = wmoInteriorTint({a, b}, 1.0f);      // intensity-weighted: (a*2 + b*6)/8
        CHECK_APPROX(mix.x, (1.0f * 2 + 0.0f * 6) / 8.0f);   // 0.25
        CHECK_APPROX(mix.z, (0.25f * 2 + 1.0f * 6) / 8.0f);  // 0.8125
        WmoLight c; c.color = {1, 1, 1}; c.intensity = 1.0f;
        CHECK(wmoInteriorTint({c}, 4.0f).x == 1.0f);   // clamped to 1.0
    }

    // --- AssetTree: listfile folder hierarchy (case-insensitive merge) ------
    {
        std::vector<std::string> paths = {
            "WORLD\\Azeroth\\Elwynn\\tree.m2",     // 0
            "World\\Azeroth\\Duskwood\\log.m2",    // 1
            "World\\wmo\\Ironforge.wmo",           // 2
            "loosefile.m2",                        // 3  (no directory)
            "WORLD\\x.m2",                         // 4
            "World\\X.m2",                         // 5  (distinct path, same dir)
        };
        AssetTree tree = buildAssetTree(paths);

        // 'WORLD\' and 'World\' merge into ONE node (MPQ paths are case-
        // insensitive) that keeps the first-seen display casing...
        CHECK(tree.root.dirs.size() == 1);
        const AssetTreeNode& world = tree.root.dirs.begin()->second;
        CHECK(world.name == "WORLD");
        // ...and both file entries survive the merge (genuinely distinct paths).
        CHECK(world.fileIndices.size() == 2);
        CHECK(world.fileIndices[0] == 4 && world.fileIndices[1] == 5);
        CHECK(world.dirs.count("azeroth") == 1 && world.dirs.count("wmo") == 1);

        // A path with no directory lands in the root.
        CHECK(tree.root.fileIndices.size() == 1 && tree.root.fileIndices[0] == 3);

        // subtreePaths returns exactly the prefix set, sorted.
        CHECK(subtreePaths(tree.root) == std::vector<int>({0, 1, 2, 3, 4, 5}));
        const AssetTreeNode& azeroth = world.dirs.at("azeroth");
        CHECK(azeroth.name == "Azeroth");
        CHECK(subtreePaths(azeroth) == std::vector<int>({0, 1}));
        CHECK(subtreePaths(world)   == std::vector<int>({0, 1, 2, 4, 5}));
        CHECK(subtreePaths(world.dirs.at("wmo")) == std::vector<int>({2}));

        // An empty listfile builds an empty tree.
        AssetTree empty = buildAssetTree({});
        CHECK(empty.root.dirs.empty() && empty.root.fileIndices.empty());
        CHECK(subtreePaths(empty.root).empty());

        // WMO group-file noise filter: _NNN.wmo (3 digits), any extension casing.
        CHECK(isWmoGroupFile("Ironforge_000.wmo"));
        CHECK(isWmoGroupFile("wmo\\Azeroth\\Ironforge_012.WMO"));
        CHECK(!isWmoGroupFile("Ironforge.wmo"));           // root WMO
        CHECK(!isWmoGroupFile("Iron_ore.wmo"));            // not three digits
        CHECK(!isWmoGroupFile("Ironforge_0000.wmo"));      // four digits
        CHECK(!isWmoGroupFile("Ironforge_000.m2"));        // wrong extension
        CHECK(!isWmoGroupFile(".wmo"));                    // degenerate
    }

    const char* mpqPath = "wforge_asset_test.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "test.blp", makeRawBlp(2, 2, Rgba{40, 200, 60, 255}) },
        { "doodad.m2", makeM2("test.blp") },
        { "wmo\\Box.wmo", makeWmoRoot() },
        { "wmo\\Box_000.wmo", makeWmoGroup() },
        { "World\\Maps\\TestMap\\TestMap.wdt", makeWdt() },
        { "World\\Maps\\TestMap\\TestMap_32_32.adt", makeAdt() },
        { "World\\Maps\\LiquidMap\\LiquidMap.wdt", makeWdt() },
        { "World\\Maps\\LiquidMap\\LiquidMap_32_32.adt", makeAdtLiquid() },
    };
    CHECK(writeMpqArchive(mpqPath, files));

    MpqManager mgr;
    CHECK(mgr.addArchive(mpqPath));
    AssetLoader loader(mgr);

    // --- listFiles: enumerate archived paths by wildcard (browser backend) ---
    {
        auto m2s = mgr.listFiles("*.m2");
        CHECK(m2s.size() == 1 && m2s[0] == "doodad.m2");
        auto wmos = mgr.listFiles("*.wmo");                  // root + group files
        CHECK(wmos.size() == 2);
        bool hasRoot = false, hasGroup = false;
        for (const auto& w : wmos) { if (w == "wmo\\Box.wmo") hasRoot = true;
                                     if (w == "wmo\\Box_000.wmo") hasGroup = true; }
        CHECK(hasRoot && hasGroup);
        CHECK(mgr.listFiles("*.m2").size() + mgr.listFiles("*.wdt").size() <= mgr.listFiles("*").size());
        CHECK(mgr.listFiles("*.dne").empty());               // no match -> empty
    }

    // --- asset_catalog: maps + placeable models for the browsers ------------
    {
        auto maps = listMaps(mgr);
        // Synthetic MPQ has World\Maps\TestMap and World\Maps\LiquidMap WDTs.
        CHECK(maps.size() == 2);
        CHECK(maps[0].name == "LiquidMap" && maps[1].name == "TestMap");   // sorted
        CHECK(maps[1].wdtPath == "World\\Maps\\TestMap\\TestMap.wdt");

        auto m2models = listModels(mgr, ModelKind::M2);
        CHECK(m2models.size() == 1 && m2models[0] == "doodad.m2");
        auto wmoModels = listModels(mgr, ModelKind::Wmo);   // root only, no _000
        CHECK(wmoModels.size() == 1 && wmoModels[0] == "wmo\\Box.wmo");
        CHECK(isWmoGroupFile("wmo\\Box_000.wmo"));
        CHECK(!isWmoGroupFile("wmo\\Box.wmo"));
    }

    // --- interactive placement: drop a model into a live scene --------------
    {
        TileScene ts;
        size_t idx = loader.placeDoodad(ts, "doodad.m2", Vec3{100, 200, 50}, 0.0f, 2.0f);
        CHECK(idx == 0 && ts.instances.size() == 1);
        const Mat4& xf = ts.instances[idx].transform;
        CHECK_APPROX(xf.at(0,3), 100.0f);
        CHECK_APPROX(xf.at(1,3), 200.0f);
        CHECK_APPROX(xf.at(2,3), 50.0f);
        CHECK(loader.placeDoodad(ts, "nope.m2", Vec3{0,0,0}) == SIZE_MAX);  // missing -> no-op
        CHECK(ts.instances.size() == 1);

        size_t widx = loader.placeWmo(ts, "wmo\\Box.wmo", Vec3{300, 0, 0}, 0.0f, 777);
        CHECK(widx != SIZE_MAX);
        CHECK(ts.wmoInstances[widx].uniqueId == 777);
        CHECK_APPROX(ts.wmoInstances[widx].transform.at(0,3), 300.0f);
    }

    // --- display-DBC resolver: spawn creatures/objects by display id --------
    {
        DbcBuilder cmd(3);                                  // CreatureModelData
        uint32_t mp = cmd.addString("Creature\\Cat\\Cat.mdx");
        cmd.addRecord({815, 0, mp});
        Dbc cmdDbc = Dbc::parse(cmd.build());

        DbcBuilder cdi(5);                                  // CreatureDisplayInfo
        cdi.addRecord({1234, 815, 0, 0, 0});                // displayId 1234 -> modelId 815
        Dbc cdiDbc = Dbc::parse(cdi.build());

        auto creatures = listCreatureModels(cdiDbc, cmdDbc);
        CHECK(creatures.size() == 1);
        CHECK(creatures[0].displayId == 1234);
        CHECK(creatures[0].model == "Creature\\Cat\\Cat.m2");   // .mdx -> .m2

        DbcBuilder godi(2);                                 // GameObjectDisplayInfo
        uint32_t gp = godi.addString("World\\wmo\\Tower.wmo");
        godi.addRecord({42, gp});
        Dbc godiDbc = Dbc::parse(godi.build());
        auto gos = listGameObjectModels(godiDbc);
        CHECK(gos.size() == 1 && gos[0].displayId == 42);
        CHECK(gos[0].model == "World\\wmo\\Tower.wmo");         // .wmo unchanged

        // GroundEffect: texture -> doodad -> detail model.
        DbcBuilder ged(2);                                  // GroundEffectDoodad
        uint32_t dp = ged.addString("Detail\\Grass01.mdx");
        ged.addRecord({777, dp});
        Dbc gedDbc = Dbc::parse(ged.build());
        DbcBuilder get(10);                                 // GroundEffectTexture
        get.addRecord({300, 777, 0, 0, 0, 0, 0, 0, 0, 32}); // doodadIds[0]=777
        Dbc getDbc = Dbc::parse(get.build());
        auto detail = listGroundEffectModels(getDbc, gedDbc);
        CHECK(detail.size() == 1 && detail[0].displayId == 777);
        CHECK(detail[0].model == "Detail\\Grass01.m2");

        // Runtime resolver: the same joins indexed by display id for O(1) lookup
        // (what resolves a server-streamed entity's UNIT_FIELD_DISPLAYID).
        DisplayResolver res;
        res.buildCreatures(cdiDbc, cmdDbc);
        res.buildGameObjects(godiDbc);
        CHECK(res.creatureCount() == 1 && res.gameObjectCount() == 1);
        CHECK(res.hasCreature(1234) && res.creatureModel(1234) == "Creature\\Cat\\Cat.m2");
        CHECK(res.hasGameObject(42) && res.gameObjectModel(42) == "World\\wmo\\Tower.wmo");
        // Cross-space and unknown ids miss cleanly (empty, not a wrong hit).
        CHECK(!res.hasCreature(42) && res.creatureModel(42).empty());
        CHECK(res.gameObjectModel(9999).empty());

        // Resolver semantics match the browser list exactly (same normalisation
        // + unresolved-skip), so no display id lists but fails to resolve.
        for (const DisplayModel& d : listCreatureModels(cdiDbc, cmdDbc))
            CHECK(res.creatureModel(d.displayId) == d.model);
    }

    // Empty DBCs: resolver builds to nothing and every lookup misses (no client).
    {
        DbcBuilder emptyCdi(5), emptyCmd(3), emptyGodi(2);
        Dbc ec = Dbc::parse(emptyCdi.build()), em = Dbc::parse(emptyCmd.build()),
            eg = Dbc::parse(emptyGodi.build());
        DisplayResolver res;
        res.buildCreatures(ec, em);
        res.buildGameObjects(eg);
        CHECK(res.creatureCount() == 0 && res.gameObjectCount() == 0);
        CHECK(res.creatureModel(1).empty() && res.gameObjectModel(1).empty());
    }

    // --- texture decode + cache + fallback ----------------------------------
    auto t = loader.texture("test.blp");
    CHECK(t && t->width == 2 && t->height == 2);
    CHECK(t->at(0,0).g > 150 && t->at(0,0).r < 100);     // the green we authored
    CHECK(loader.texture("test.blp").get() == t.get());  // cached (same object)
    auto miss = loader.texture("nope.blp");
    CHECK(miss && miss->width > 0);                      // fallback, no crash

    // --- WDT: the tile we flagged exists ------------------------------------
    Wdt wdt;
    CHECK(loader.loadWdt("TestMap", wdt));
    CHECK(wdt.tiles[32*64 + 32]);
    CHECK(!wdt.tiles[0]);

    // --- ADT: chunks + MTEX names -------------------------------------------
    Adt adt; std::vector<MapChunk> chunks;
    CHECK(loader.loadAdt("TestMap", 32, 32, adt, chunks));
    CHECK(chunks.size() == 1);
    CHECK(adt.textures.size() == 1 && adt.textures[0] == "test.blp");
    CHECK(chunks[0].layers.size() == 1 && chunks[0].layers[0].textureId == 0);

    // --- buildTile: textured terrain wired to the decoded BLP ---------------
    TileRender tile = loader.buildTile("TestMap", 32, 32);
    CHECK(!tile.empty());
    CHECK(tile.chunkMeshes.size() == 1);
    CHECK(tile.chunkLayers.size() == 1 && tile.chunkLayers[0].size() == 1);
    CHECK(tile.chunkLayers[0][0].texture == t.get());    // layer 0 -> our green BLP

    // It renders the green terrain.
    Framebuffer fb(64, 64); fb.clear(Rgba{0,0,0,255});
    Mat4 view = Mat4::lookAt({-20,-20,40}, {-16,-16,0}, {0,0,1});
    Mat4 proj = Mat4::perspective(60.0, 1.0, 1.0, 500.0);
    tile.renderTerrain(fb, proj * view, {0,0,1});
    bool green = false;
    for (const Rgba& p : fb.color.pixels) if (p.g > 80 && p.g > p.r && p.g > p.b) { green = true; break; }
    CHECK(green);

    // --- model(): M2 parse + cache + missing -------------------------------
    auto mdl = loader.model("doodad.m2");
    CHECK(mdl && mdl->vertices.size() == 3);
    CHECK(loader.model("doodad.m2").get() == mdl.get());   // cached
    CHECK(loader.model("missing.m2") == nullptr);          // graceful nullptr

    // modelBounds: real AABB from the model's vertices; invalid for a miss.
    Aabb mb = loader.modelBounds("doodad.m2");
    CHECK(mb.valid() && mb.radius() > 0.0f);
    CHECK(!loader.modelBounds("missing.m2").valid());

    // --- buildTileScene: terrain + placed doodads --------------------------
    TileScene scene = loader.buildTileScene("TestMap", 32, 32);
    CHECK(!scene.terrain.empty());
    // Two MDDF entries authored; only doodad.m2 resolves -> exactly one instance.
    CHECK(scene.doodadCount() == 1);
    // The WMO placement loaded its group geometry into a pickable instance.
    CHECK(scene.wmoCount() == 1);
    CHECK(!scene.wmoInstances[0].mesh.indices.empty());
    CHECK(scene.wmoInstances[0].uniqueId == 2001);
    // ...and a textured render part whose material resolves to our green BLP.
    CHECK(scene.wmoRenderInstances.size() == 1);
    CHECK(scene.textures[scene.wmoRenderInstances[0].tex].get() == t.get());
    CHECK(!scene.meshes[scene.wmoRenderInstances[0].mesh].vertices.empty());
    // Pools hold the doodad (index 0) + the WMO render part (index 1).
    CHECK(scene.meshes.size() == 2 && scene.textures.size() == 2);
    CHECK(!scene.meshes[0].vertices.empty());
    CHECK(scene.textures[0].get() == t.get());             // doodad MTEX -> green BLP

    // The instance transform translates to the placement's world position.
    Vec3 world = placementToWorld(Vec3{100.f, 200.f, 300.f});
    const Mat4& m = scene.instances[0].transform;
    CHECK_APPROX(m.at(0,3), world.x);
    CHECK_APPROX(m.at(1,3), world.y);
    CHECK_APPROX(m.at(2,3), world.z);

    // The missing doodad still left a marker (skipped, not fatal).
    CHECK(scene.markers.stats().points > 0 || scene.markers.stats().lines > 0);

    // It renders without crashing and puts green terrain on screen.
    Framebuffer fb2(64, 64); fb2.clear(Rgba{0,0,0,255});
    scene.render(fb2, proj * view, {0,0,1});

    // --- a missing tile loads as empty, not a crash -------------------------
    TileRender none = loader.buildTile("TestMap", 5, 5);
    CHECK(none.empty());
    CHECK(loader.buildTileScene("TestMap", 5, 5).terrain.empty());

    // --- liquid tile: the built scene carries a translucent water surface ----
    {
        TileRender lt = loader.buildTile("LiquidMap", 32, 32);
        CHECK(!lt.empty());
        CHECK(lt.hasLiquid());                          // a liquid surface was built
        CHECK(lt.liquids.size() == 1);
        const LiquidSurface& ls = lt.liquids[0];
        CHECK(ls.type == LiquidType::River);
        CHECK(!ls.emissive);                            // water is shaded, not glowing
        CHECK(ls.tint.a < 255);                         // translucent
        // 64 rendered 8x8 cells * 2 triangles * 3 indices.
        CHECK(ls.mesh.indices.size() == 64u * 2u * 3u);
        CHECK(!ls.mesh.vertices.empty());

        // The full scene also carries the liquid and renders without crashing.
        TileScene lscene = loader.buildTileScene("LiquidMap", 32, 32);
        CHECK(lscene.terrain.hasLiquid());

        // Render and confirm the translucent water actually composites pixels:
        // a frame with the liquid pass differs from terrain-only (water tints
        // the surface bluish). Camera looks straight down at the chunk.
        const int LW = 64, LH = 64;
        Vec3 lo{+1e30f,+1e30f,+1e30f}, hi{-1e30f,-1e30f,-1e30f};
        for (const auto& cm : lscene.terrain.chunkMeshes)
            for (const auto& v : cm.vertices) {
                lo.x=std::min(lo.x,v.position.x); lo.y=std::min(lo.y,v.position.y); lo.z=std::min(lo.z,v.position.z);
                hi.x=std::max(hi.x,v.position.x); hi.y=std::max(hi.y,v.position.y); hi.z=std::max(hi.z,v.position.z);
            }
        Vec3 ctr{(lo.x+hi.x)*0.5f,(lo.y+hi.y)*0.5f,(lo.z+hi.z)*0.5f};
        float rr = length(hi-ctr)+5.0f;
        Mat4 lview = Mat4::lookAt(ctr + Vec3{0.1f,0.1f,rr}, ctr, {0,1,0});
        Mat4 lproj = Mat4::perspective(55.0, double(LW)/LH, 1.0, rr*6.0+100.0);
        Mat4 lvp = lproj * lview;

        Framebuffer fbTerrain(LW, LH); fbTerrain.clear(Rgba{0,0,0,255});
        lscene.terrain.renderTerrain(fbTerrain, lvp, {0,0,1});
        Framebuffer fbWater(LW, LH); fbWater.clear(Rgba{0,0,0,255});
        lscene.terrain.renderTerrain(fbWater, lvp, {0,0,1});
        lscene.terrain.renderLiquid(fbWater, lvp, {0,0,1});

        int changed = 0, bluer = 0;
        for (size_t i = 0; i < fbWater.color.pixels.size(); ++i) {
            const Rgba& a = fbTerrain.color.pixels[i];
            const Rgba& b = fbWater.color.pixels[i];
            if (a.r!=b.r || a.g!=b.g || a.b!=b.b) ++changed;
            if (b.b > a.b) ++bluer;                     // water tints toward blue
        }
        CHECK(changed > 0);                             // the liquid pass drew pixels
        CHECK(bluer > 0);                               // and tinted them bluish
    }

    std::remove(mpqPath);

    // --- WDT MPHD big-alpha flag drives the MCAL format selection -----------
    // Same ramp blob byte k == (k & 0xFF). As 8-bit "big alpha" it is one byte
    // per texel, so texel 1 decodes to 1. As packed 4-bit it is two texels per
    // byte (low nibble first), so texel 1 is the *high* nibble of byte 0 (== 0).
    {
        std::vector<uint8_t> ramp(4096);
        for (int k = 0; k < 4096; ++k) ramp[k] = static_cast<uint8_t>(k & 0xFF);

        // parseWdt surfaces the flag, and Wdt::bigAlpha() derives from it.
        CHECK(parseWdt(makeWdt(0x0)).bigAlpha() == false);
        CHECK(parseWdt(makeWdt(0x4)).bigAlpha() == true);
        CHECK(parseWdt(makeWdt(0x4)).mphdFlags == 0x4u);
        CHECK(parseWdt(makeWdt(0x5)).globalWmo == true);   // 0x1 set alongside 0x4
        CHECK(parseWdt(makeWdt(0x5)).bigAlpha() == true);

        auto buildWith = [&](uint32_t mphdFlags) -> AlphaMap {
            const char* p = "wforge_bigalpha_test.mpq";
            std::vector<std::pair<std::string, std::vector<uint8_t>>> f = {
                { "test.blp", makeRawBlp(2, 2, Rgba{40, 200, 60, 255}) },
                { "World\\Maps\\BAMap\\BAMap.wdt", makeWdt(mphdFlags) },
                { "World\\Maps\\BAMap\\BAMap_32_32.adt", makeAdt2Layer(ramp) },
            };
            CHECK(writeMpqArchive(p, f));
            MpqManager m; CHECK(m.addArchive(p));
            AssetLoader l(m);
            // No explicit bigAlpha override -> auto-derived from the WDT MPHD flag.
            TileRender tr = l.buildTile("BAMap", 32, 32);
            CHECK(!tr.empty());
            CHECK(tr.chunkAlphas.size() == 1 && tr.chunkAlphas[0].size() == 1);
            AlphaMap am = tr.chunkAlphas[0][0];
            std::remove(p);
            return am;
        };

        AlphaMap packed4 = buildWith(0x0);    // flag clear -> packed 4-bit
        AlphaMap big8    = buildWith(0x4);    // flag set   -> 8-bit big alpha

        // Texel 0 is byte 0 == 0 in both forms.
        CHECK(packed4.at(0, 0) == 0);
        CHECK(big8.at(0, 0) == 0);
        // Texel 1 is where the two formats diverge -- proof the flag was honoured.
        CHECK(packed4.at(0, 1) == 0);         // high nibble of byte 0 (0x0) * 17
        CHECK(big8.at(0, 1) == 1);            // byte 1 == 1
        // A few more: byte 16 == 16 -> big alpha texel 16; 4-bit texel 16 = low
        // nibble of byte 8 (== 8) * 17 == 136.
        CHECK(big8.texels[16] == 16);
        CHECK(packed4.texels[16] == 8 * 17);

        // Explicit override still wins over the WDT flag (test/editor escape hatch).
        {
            const char* p = "wforge_bigalpha_ovr.mpq";
            std::vector<std::pair<std::string, std::vector<uint8_t>>> f = {
                { "test.blp", makeRawBlp(2, 2, Rgba{40, 200, 60, 255}) },
                { "World\\Maps\\BAMap\\BAMap.wdt", makeWdt(0x0) },   // flag clear
                { "World\\Maps\\BAMap\\BAMap_32_32.adt", makeAdt2Layer(ramp) },
            };
            CHECK(writeMpqArchive(p, f));
            MpqManager m; CHECK(m.addArchive(p));
            AssetLoader l(m);
            TileRender tr = l.buildTile("BAMap", 32, 32, /*bigAlpha*/true);  // force 8-bit
            CHECK(tr.chunkAlphas[0][0].at(0, 1) == 1);   // decoded as big alpha
            std::remove(p);
        }
    }
}
