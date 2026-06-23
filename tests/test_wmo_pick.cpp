#include "test.hpp"
#include "asset_loader.hpp"
#include "bounds.hpp"
#include "picking.hpp"
#include "scene_pick.hpp"
#include "mpq.hpp"
#include "wmo.hpp"
#include "math.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void putf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(b,v); }
void magic(std::vector<uint8_t>& b, const char* m){ b.push_back(m[3]); b.push_back(m[2]); b.push_back(m[1]); b.push_back(m[0]); }
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p){
    magic(b,m); put32(b,(uint32_t)p.size()); b.insert(b.end(),p.begin(),p.end());
}

// A minimal WMO root: one group, bbox enclosing the group triangle.
std::vector<uint8_t> makeWmoRoot() {
    std::vector<uint8_t> root, mohd;
    put32(mohd,0); put32(mohd,1); put32(mohd,0); put32(mohd,0);   // nTex,nGroups,nPortals,nLights
    put32(mohd,0); put32(mohd,0); put32(mohd,0);                  // doodadNames/Defs/Sets
    put32(mohd,0); put32(mohd,1);                                 // ambColor, wmoID
    putf(mohd,0);putf(mohd,0);putf(mohd,0);                       // bbox min
    putf(mohd,4);putf(mohd,4);putf(mohd,1);                       // bbox max (loose)
    put16(mohd,0); put16(mohd,0);
    chunk(root,"MOHD",mohd);
    std::vector<uint8_t> mogn; mogn.push_back(0); mogn.push_back(0);
    chunk(root,"MOGN",mogn);
    std::vector<uint8_t> mogi;
    put32(mogi,0x8); putf(mogi,0);putf(mogi,0);putf(mogi,0); putf(mogi,4);putf(mogi,4);putf(mogi,1);
    put32(mogi,0);
    chunk(root,"MOGI",mogi);
    return root;
}

// A group with a single triangle (0,0,0)-(4,0,0)-(0,4,0) in the z=0 plane.
std::vector<uint8_t> makeWmoGroup() {
    std::vector<uint8_t> movt;
    putf(movt,0);putf(movt,0);putf(movt,0);
    putf(movt,4);putf(movt,0);putf(movt,0);
    putf(movt,0);putf(movt,4);putf(movt,0);
    std::vector<uint8_t> monr; for(int i=0;i<3;i++){ putf(monr,0);putf(monr,0);putf(monr,1); }
    std::vector<uint8_t> motv; for(int i=0;i<3;i++){ putf(motv,0);putf(motv,0); }
    std::vector<uint8_t> movi; put16(movi,0); put16(movi,1); put16(movi,2);
    std::vector<uint8_t> mopy; mopy.push_back(0); mopy.push_back(0);

    std::vector<uint8_t> mogp(0x44, 0);
    { uint32_t fl=0x8; for(int i=0;i<4;i++) mogp[0x08+i]=(fl>>(8*i))&0xFF; }
    chunk(mogp,"MOVT",movt); chunk(mogp,"MONR",monr); chunk(mogp,"MOTV",motv);
    chunk(mogp,"MOVI",movi); chunk(mogp,"MOPY",mopy);
    std::vector<uint8_t> group; chunk(group,"MOGP",mogp);
    return group;
}
} // namespace

void test_wmo_pick() {
    std::printf("[wmo_pick]\n");

    const char* mpqPath = "wforge_wmo_test.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "World\\wmo\\Box.wmo",     makeWmoRoot() },
        { "World\\wmo\\Box_000.wmo", makeWmoGroup() },
    };
    CHECK(writeMpqArchive(mpqPath, files));

    MpqManager mgr; CHECK(mgr.addArchive(mpqPath));
    AssetLoader loader(mgr);

    // --- load root + group geometry -----------------------------------------
    auto wmo = loader.wmo("World\\wmo\\Box.wmo");
    CHECK(wmo && wmo->root.nGroups == 1);
    CHECK(wmo->groups.size() == 1);
    CHECK(wmo->groups[0].vertices.size() == 3 && wmo->groups[0].indices.size() == 3);
    CHECK(loader.wmo("World\\wmo\\Box.wmo").get() == wmo.get());   // cached
    CHECK(loader.wmo("World\\wmo\\Missing.wmo") == nullptr);

    // --- bounds from geometry are tighter than the (loose) root bbox --------
    Aabb geom = wmoBounds(*wmo);
    CHECK(geom.valid());
    CHECK_APPROX(geom.max.x, 4.0f);           // the triangle, not the 4x4x1 root box
    CHECK_APPROX(geom.max.z, 0.0f);           // flat -> z extent 0 (root said 1)

    // --- pick the actual triangle, not just its bounding box ----------------
    Mesh pm = wmoPickMesh(*wmo);
    CHECK(pm.indices.size() == 3);

    auto down = [](float x, float y){ return Ray{ Vec3{x,y,10}, Vec3{0,0,-1} }; };

    // A ray over the triangle interior hits it at z=0 (t = 10).
    float tHit = pickMeshXform(down(1.0f, 1.0f), pm, Mat4::identity());
    CHECK(tHit > 0.0f);
    CHECK_APPROX(tHit, 10.0f);

    // A ray over the box's empty corner (x+y > 4) is INSIDE the AABB but misses
    // the triangle -- the whole point of per-triangle picking.
    CHECK(pickMeshXform(down(3.5f, 3.5f), pm, Mat4::identity()) < 0.0f);

    // --- placed by a transform (a MODF-style instance) ----------------------
    // Translate the WMO by +100 in X; the same local interior point is now at
    // x in [100,104]. A world ray there hits; the un-translated spot misses.
    Mat4 place = Mat4::translate(Vec3{100, 0, 0});
    CHECK(pickMeshXform(down(101.0f, 1.0f), pm, place) > 0.0f);
    CHECK(pickMeshXform(down(1.0f,   1.0f), pm, place) < 0.0f);

    std::remove(mpqPath);

    // --- pickScene: terrain vs doodad vs WMO, nearest wins ------------------
    TileScene scene;
    // A big flat terrain chunk on z=0.
    TexMesh ground;
    ground.vertices = {
        { Vec3{-50,-50,0}, Vec3{0,0,1}, Vec2{0,0} },
        { Vec3{ 50,-50,0}, Vec3{0,0,1}, Vec2{0,0} },
        { Vec3{ 50, 50,0}, Vec3{0,0,1}, Vec2{0,0} },
        { Vec3{-50, 50,0}, Vec3{0,0,1}, Vec2{0,0} },
    };
    ground.indices = { 0,1,2, 0,2,3 };
    scene.terrain.chunkMeshes.push_back(ground);

    // A doodad: a small quad floating at z=5 around (10,10).
    TexMesh dood;
    dood.vertices = {
        { Vec3{-1,-1,0}, Vec3{0,0,1}, Vec2{0,0} },
        { Vec3{ 1,-1,0}, Vec3{0,0,1}, Vec2{0,0} },
        { Vec3{ 1, 1,0}, Vec3{0,0,1}, Vec2{0,0} },
        { Vec3{-1, 1,0}, Vec3{0,0,1}, Vec2{0,0} },
    };
    dood.indices = { 0,1,2, 0,2,3 };
    scene.meshes.push_back(dood);
    scene.instances.push_back({ 0, 0, Mat4::translate(Vec3{10,10,5}) });

    // A WMO instance (the triangle pick mesh) placed at (30,0,2).
    scene.wmoInstances.push_back({ pm, Mat4::translate(Vec3{30,0,2}), 7777u });

    // Over open ground -> terrain.
    ScenePick sg = pickScene(down(-20.0f, -20.0f), scene);
    CHECK(sg.hit() && sg.kind == ScenePick::Kind::Terrain);
    CHECK_APPROX(sg.point.z, 0.0f);

    // Over the doodad -> the doodad (z=5) wins over the ground beneath it.
    ScenePick sd = pickScene(down(10.0f, 10.0f), scene);
    CHECK(sd.hit() && sd.kind == ScenePick::Kind::Doodad);
    CHECK_APPROX(sd.point.z, 5.0f);

    // Over the WMO triangle interior (local (1,1) + offset 30,0 -> world ~31,1).
    ScenePick sw = pickScene(down(31.0f, 1.0f), scene);
    CHECK(sw.hit() && sw.kind == ScenePick::Kind::Wmo);
    CHECK(sw.uniqueId == 7777u);
    CHECK_APPROX(sw.point.z, 2.0f);

    // A ray into empty space misses everything.
    CHECK(!pickScene(Ray{ Vec3{0,0,10}, Vec3{0,0,1} }, scene).hit());

    // --- pickWorld: entity stream vs static scene, nearest wins -------------
    {
        WorldView wv;
        EntityState e; e.guid = 42; e.pos = {31,1,5}; e.boundingRadius = 1.0f;
        wv.apply(e);                              // floats above the WMO at z=2
        WorldPick wp = pickWorld(down(31.0f, 1.0f), wv, scene);
        CHECK(wp.isEntity() && wp.guid == 42);    // entity (z=5) nearer than WMO (z=2)

        // With no entities, the same click falls through to the WMO triangle.
        WorldView empty;
        WorldPick ws2 = pickWorld(down(31.0f, 1.0f), empty, scene);
        CHECK(ws2.isScene() && ws2.kind == WorldPick::Kind::Wmo && ws2.uniqueId == 7777u);

        // Open ground with no entities -> terrain.
        WorldPick wg2 = pickWorld(down(-20.0f, -20.0f), empty, scene);
        CHECK(wg2.isScene() && wg2.kind == WorldPick::Kind::Terrain);

        // A miss everywhere.
        CHECK(!pickWorld(Ray{ Vec3{0,0,10}, Vec3{0,0,1} }, empty, scene).hit());
    }
}
