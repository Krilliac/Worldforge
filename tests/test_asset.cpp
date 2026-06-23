#include "test.hpp"
#include "asset_loader.hpp"
#include "mpq.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"
#include "coords.hpp"

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
    for (int i=0;i<145;i++){ mcnr.push_back(0); mcnr.push_back(127); mcnr.push_back(0); }
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
    p32(mohd,0); p32(mohd,1); p32(mohd,0); p32(mohd,0);   // nTex,nGroups,nPortals,nLights
    p32(mohd,0); p32(mohd,0); p32(mohd,0);                // doodadNames/Defs/Sets
    p32(mohd,0); p32(mohd,1);                             // ambColor, wmoID
    pf(mohd,0);pf(mohd,0);pf(mohd,0); pf(mohd,4);pf(mohd,4);pf(mohd,1);   // bbox
    p16(mohd,0); p16(mohd,0);
    chunk(root,"MOHD",mohd);
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

std::vector<uint8_t> makeWdt() {
    std::vector<uint8_t> mphd; p32(mphd, 0);            // flags = 0 (4-bit alpha)
    std::vector<uint8_t> main(64*64*8, 0);
    size_t e = (size_t)(32*64 + 32) * 8;                // tile (x=32,y=32)
    main[e] = 1;                                        // flags & 1 -> has ADT
    std::vector<uint8_t> wdt;
    chunk(wdt, "MPHD", mphd);
    chunk(wdt, "MAIN", main);
    return wdt;
}
} // namespace

void test_asset() {
    std::printf("[asset]\n");

    const char* mpqPath = "wforge_asset_test.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "test.blp", makeRawBlp(2, 2, Rgba{40, 200, 60, 255}) },
        { "doodad.m2", makeM2("test.blp") },
        { "wmo\\Box.wmo", makeWmoRoot() },
        { "wmo\\Box_000.wmo", makeWmoGroup() },
        { "World\\Maps\\TestMap\\TestMap.wdt", makeWdt() },
        { "World\\Maps\\TestMap\\TestMap_32_32.adt", makeAdt() },
    };
    CHECK(writeMpqArchive(mpqPath, files));

    MpqManager mgr;
    CHECK(mgr.addArchive(mpqPath));
    AssetLoader loader(mgr);

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
    CHECK(scene.meshes.size() == 1 && scene.textures.size() == 1);
    CHECK(!scene.meshes[0].vertices.empty());
    CHECK(scene.textures[0].get() == t.get());             // model's MTEX -> green BLP

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

    std::remove(mpqPath);
}
