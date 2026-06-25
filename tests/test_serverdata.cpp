#include "test.hpp"
#include "dbc_defs.hpp"
#include "gridmap.hpp"
#include "navmesh.hpp"
#include "vmap.hpp"
#include "mpq.hpp"
#include "wow_files.hpp"

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

// Build a WDBC blob: records is a list of rows, each `fieldCount` uint32 values
// (floats are pre-encoded as their bit pattern). strblock is the string table.
std::vector<uint8_t> makeDbc(uint32_t fieldCount,
                             const std::vector<std::vector<uint32_t>>& records,
                             const std::string& strblock) {
    std::vector<uint8_t> b;
    praw(b, "WDBC");
    p32(b, (uint32_t)records.size());
    p32(b, fieldCount);
    p32(b, fieldCount * 4);                 // recordSize
    p32(b, (uint32_t)strblock.size());      // stringSize
    for (const auto& rec : records)
        for (uint32_t f = 0; f < fieldCount; ++f)
            p32(b, f < rec.size() ? rec[f] : 0u);
    b.insert(b.end(), strblock.begin(), strblock.end());
    return b;
}
uint32_t fbits(float f){ uint32_t v; std::memcpy(&v,&f,4); return v; }
} // namespace

void test_dbc_defs() {
    std::printf("[dbc_defs]\n");

    // string block: offset 0 = "", then "Azeroth" @1, "Elwynn Forest" @9.
    std::string strs;
    strs.push_back('\0');
    size_t offAzeroth = strs.size(); strs += "Azeroth"; strs.push_back('\0');
    size_t offElwynn  = strs.size(); strs += "Elwynn Forest"; strs.push_back('\0');

    // --- Map.dbc: id, dir(str), instanceType, _, name(str) ---
    {
        std::vector<uint32_t> rec(42, 0);
        rec[0] = 0;                                  // id
        rec[1] = (uint32_t)offAzeroth;               // directory
        rec[2] = 0;                                  // instanceType (world)
        rec[4] = (uint32_t)offAzeroth;               // name (enUS)
        Dbc dbc = Dbc::parse(makeDbc(42, { rec }, strs));
        MapEntry e = mapEntry(dbc, 0);
        CHECK(e.id == 0 && e.instanceType == 0);
        CHECK(e.directory == "Azeroth");
        CHECK(e.name == "Azeroth");
    }

    // --- AreaTable.dbc: id, map, parent, bit, flags, ... level@10, name@11 ---
    {
        std::vector<uint32_t> rec(25, 0);
        rec[0]  = 12;                                // id
        rec[1]  = 0;                                 // map
        rec[2]  = 1519;                              // parent area
        rec[3]  = 5;                                 // area bit
        rec[4]  = 0x40;                              // flags
        rec[10] = 1;                                 // exploration level
        rec[11] = (uint32_t)offElwynn;               // name enUS
        Dbc dbc = Dbc::parse(makeDbc(25, { rec }, strs));
        AreaEntry e = areaEntry(dbc, 0);
        CHECK(e.id == 12 && e.parentAreaId == 1519);
        CHECK(e.flags == 0x40 && e.explorationLevel == 1);
        CHECK(e.name == "Elwynn Forest");
    }

    // --- LiquidType.dbc: id, liquidId, type, spell ---
    {
        std::vector<uint32_t> rec = { 5, 23, 3, 0 };  // water
        Dbc dbc = Dbc::parse(makeDbc(4, { rec }, std::string(1, '\0')));
        LiquidTypeEntry e = liquidTypeEntry(dbc, 0);
        CHECK(e.id == 5 && e.liquidId == 23 && e.type == 3);
    }

    // --- Light.dbc: id, map, x,y,z, falloffStart/End, params[5] ---
    // Real vanilla 1.12 Light.dbc has exactly 12 fields => 5 LightParams refs
    // (fields 7..11). Reading 8 params (the old layout) overran fields 12..14 and
    // crashed on the real dbc; this fixture matches the real shape so the field
    // count is a regression guard.
    {
        std::vector<uint32_t> rec(12, 0);
        rec[0] = 1; rec[1] = 0;
        rec[2] = fbits(-9000.0f); rec[3] = fbits(100.0f); rec[4] = fbits(50.0f);
        rec[5] = fbits(0.0f); rec[6] = fbits(1000.0f);
        rec[7] = 396; rec[11] = 7;                    // first and last LightParams
        Dbc dbc = Dbc::parse(makeDbc(12, { rec }, std::string(1, '\0')));
        CHECK(dbc.fieldCount() == 12);
        LightEntry e = lightEntry(dbc, 0);
        CHECK(e.id == 1 && e.mapId == 0);
        CHECK_APPROX(e.x, -9000.0f);
        CHECK_APPROX(e.falloffEnd, 1000.0f);
        CHECK(e.lightParams.size() == 5);
        CHECK(e.lightParams[0] == 396 && e.lightParams[4] == 7);
    }

    // --- CreatureModelData.dbc: id, _, modelPath@2(str) ---
    {
        std::string ms;
        ms.push_back('\0');
        size_t offCat = ms.size(); ms += "Creature\\Cat\\Cat.mdx"; ms.push_back('\0');
        std::vector<uint32_t> rec(10, 0);
        rec[0] = 815;                                 // id
        rec[2] = (uint32_t)offCat;                    // modelPath
        Dbc dbc = Dbc::parse(makeDbc(10, { rec }, ms));
        CreatureModelDataEntry e = creatureModelDataEntry(dbc, 0);
        CHECK(e.id == 815);
        CHECK(e.modelPath == "Creature\\Cat\\Cat.mdx");
        CHECK(normalizeModelPath(e.modelPath) == "Creature\\Cat\\Cat.m2");
    }

    // --- CreatureDisplayInfo.dbc: id, modelId@1, _, _, scale@4 ---
    {
        std::vector<uint32_t> rec(13, 0);
        rec[0] = 1234;                                // id (Displayid)
        rec[1] = 815;                                 // modelId -> CreatureModelData.id
        rec[4] = fbits(1.5f);                         // scale (VERIFY-FLAGGED)
        Dbc dbc = Dbc::parse(makeDbc(13, { rec }, std::string(1, '\0')));
        CreatureDisplayInfoEntry e = creatureDisplayInfoEntry(dbc, 0);
        CHECK(e.id == 1234 && e.modelId == 815);
        CHECK_APPROX(e.scale, 1.5f);
    }

    // --- GameObjectDisplayInfo.dbc: id, modelName@1(str) ---
    {
        std::string gs;
        gs.push_back('\0');
        size_t offWmo = gs.size(); gs += "World\\wmo\\Azeroth\\Buildings\\Tower.wmo"; gs.push_back('\0');
        std::vector<uint32_t> rec(18, 0);
        rec[0] = 42;                                  // id (Displayid)
        rec[1] = (uint32_t)offWmo;                    // modelName
        Dbc dbc = Dbc::parse(makeDbc(18, { rec }, gs));
        GameObjectDisplayInfoEntry e = gameObjectDisplayInfoEntry(dbc, 0);
        CHECK(e.id == 42);
        CHECK(e.modelName == "World\\wmo\\Azeroth\\Buildings\\Tower.wmo");
        CHECK(normalizeModelPath(e.modelName) == "World\\wmo\\Azeroth\\Buildings\\Tower.wmo");
    }

    // --- normalizeModelPath: .mdx/.mdl -> .m2 (case-insensitive); others unchanged ---
    CHECK(normalizeModelPath("X\\Y.mdx") == "X\\Y.m2");
    CHECK(normalizeModelPath("a.MDL") == "a.m2");
    CHECK(normalizeModelPath("z.wmo") == "z.wmo");
}

void test_gridmap() {
    std::printf("[gridmap]\n");

    std::vector<uint8_t> area, height, liquid, holes;

    // AREA: non-uniform 16x16, cell (r,c) = r*16+c.
    praw(area, "AREA"); p16(area, 0); p16(area, 0);
    for (int i = 0; i < 16 * 16; ++i) p16(area, (uint16_t)i);

    // MHGT: absolute floats; V9[y*129+x] = x + y, V8 = 0.
    praw(height, "MHGT"); p32(height, 0); pf(height, 0.0f); pf(height, 500.0f);
    for (int y = 0; y < GRIDMAP_V9; ++y)
        for (int x = 0; x < GRIDMAP_V9; ++x) pf(height, (float)(x + y));
    for (int i = 0; i < GRIDMAP_V8 * GRIDMAP_V8; ++i) pf(height, 0.0f);

    // MLIQ: typed + height, 4x4 surface at level 42.
    praw(liquid, "MLIQ"); p16(liquid, 0); p16(liquid, 8 /*water*/);
    p8(liquid, 0); p8(liquid, 0); p8(liquid, 4); p8(liquid, 4); pf(liquid, 42.0f);
    for (int i = 0; i < 16 * 16; ++i) p16(liquid, 1);   // entry
    for (int i = 0; i < 16 * 16; ++i) p8(liquid, 0);    // flags
    for (int i = 0; i < 4 * 4; ++i) pf(liquid, 42.0f);  // surface heights

    // HOLES: 16x16 uint16; cell 0 = 0xFFFF.
    p16(holes, 0xFFFF);
    for (int i = 1; i < 16 * 16; ++i) p16(holes, 0);

    // Assemble header + sections with computed offsets.
    const uint32_t hdr = 44;
    const uint32_t areaOfs = hdr;
    const uint32_t heightOfs = areaOfs + (uint32_t)area.size();
    const uint32_t liquidOfs = heightOfs + (uint32_t)height.size();
    const uint32_t holesOfs  = liquidOfs + (uint32_t)liquid.size();

    std::vector<uint8_t> buf;
    praw(buf, "MAPS"); p32(buf, 0x352e317a /*'z1.5'*/); p32(buf, 5875);
    p32(buf, areaOfs);   p32(buf, (uint32_t)area.size());
    p32(buf, heightOfs); p32(buf, (uint32_t)height.size());
    p32(buf, liquidOfs); p32(buf, (uint32_t)liquid.size());
    p32(buf, holesOfs);  p32(buf, (uint32_t)holes.size());
    buf.insert(buf.end(), area.begin(),   area.end());
    buf.insert(buf.end(), height.begin(), height.end());
    buf.insert(buf.end(), liquid.begin(), liquid.end());
    buf.insert(buf.end(), holes.begin(),  holes.end());

    GridMap m = parseGridMap(buf);
    CHECK(m.buildMagic == 5875);
    CHECK(m.hasArea && !m.area.uniform);
    CHECK(m.area.grid[16 + 2] == 18);             // cell (1,2)
    CHECK(m.hasHeightSection && m.height.present);
    CHECK_APPROX(m.heightV9(0, 0), 0.0f);
    CHECK_APPROX(m.heightV9(10, 5), 15.0f);       // x+y
    CHECK_APPROX(m.heightV9(128, 128), 256.0f);
    CHECK(m.hasLiquid && m.liquid.present);
    CHECK(m.liquid.liquidType == 8 && m.liquid.width == 4);
    CHECK_APPROX(m.liquid.liquidLevel, 42.0f);
    CHECK(m.liquid.heightMap.size() == 16);
    CHECK(m.liquid.entry.size() == 256 && m.liquid.entry[0] == 1);
    CHECK(m.hasHoles && m.holes[0] == 0xFFFF && m.holes[1] == 0);

    // --- uniform-area + NO_HEIGHT variant ---
    std::vector<uint8_t> area2, height2;
    praw(area2, "AREA"); p16(area2, 0x0001 /*NO_AREA*/); p16(area2, 77);
    praw(height2, "MHGT"); p32(height2, 0x0001 /*NO_HEIGHT*/); pf(height2, 64.0f); pf(height2, 64.0f);
    std::vector<uint8_t> buf2;
    praw(buf2, "MAPS"); p32(buf2, 0); p32(buf2, 0);
    uint32_t a2 = 44, h2 = a2 + (uint32_t)area2.size();
    p32(buf2, a2); p32(buf2, (uint32_t)area2.size());
    p32(buf2, h2); p32(buf2, (uint32_t)height2.size());
    p32(buf2, 0); p32(buf2, 0); p32(buf2, 0); p32(buf2, 0);
    buf2.insert(buf2.end(), area2.begin(), area2.end());
    buf2.insert(buf2.end(), height2.begin(), height2.end());
    GridMap m2 = parseGridMap(buf2);
    CHECK(m2.area.uniform && m2.area.uniformArea == 77);
    CHECK(!m2.height.present);
    CHECK_APPROX(m2.heightV9(50, 50), 64.0f);     // flat fallback
    CHECK(!m2.hasLiquid && !m2.hasHoles);
}

void test_navmesh() {
    std::printf("[navmesh]\n");

    std::vector<uint8_t> buf;
    // MmapTileHeader (20): MMAP magic, dtVersion 7, mmapVersion 5, size, usesLiquids.
    p32(buf, 0x4d4d4150); p32(buf, 7); p32(buf, 5); p32(buf, 0); p32(buf, 0);
    // dtMeshHeader (100).
    p32(buf, 0x444E4156);                          // DT_NAVMESH_MAGIC
    p32(buf, 7);                                   // version
    p32(buf, 3); p32(buf, 4);                      // x, y
    p32(buf, 0); p32(buf, 0);                      // layer, userId
    p32(buf, 1); p32(buf, 4);                      // polyCount, vertCount
    for (int i = 0; i < 7; ++i) p32(buf, 0);       // 7 more int counts
    pf(buf, 1.0f); pf(buf, 0.5f); pf(buf, 0.2f);   // walkable h/r/climb
    pf(buf, 0); pf(buf, 0); pf(buf, 0);            // bmin
    pf(buf, 10); pf(buf, 10); pf(buf, 10);         // bmax
    pf(buf, 1.0f);                                 // bvQuantFactor
    // 4 vertices (Detour space rx,ry,rz).
    pf(buf, 0); pf(buf, 0); pf(buf, 0);
    pf(buf, 10); pf(buf, 0); pf(buf, 0);
    pf(buf, 10); pf(buf, 0); pf(buf, 10);
    pf(buf, 0); pf(buf, 0); pf(buf, 10);
    // 1 poly (32 bytes): a quad over verts 0..3.
    p32(buf, 0);                                   // firstLink
    p16(buf, 0); p16(buf, 1); p16(buf, 2); p16(buf, 3); p16(buf, 0); p16(buf, 0);
    for (int i = 0; i < 6; ++i) p16(buf, 0);       // neis
    p16(buf, 0x01);                                // flags (NAV_GROUND)
    p8(buf, 4);                                    // vertCount
    p8(buf, 0x09);                                 // areaAndType -> area 9

    NavTile t = parseMmTile(buf);
    CHECK(t.tileX == 3 && t.tileY == 4);
    CHECK(t.verts.size() == 4);
    CHECK(t.polys.size() == 1);
    CHECK(t.polys[0].vertCount == 4);
    CHECK(t.polys[0].area == 9);
    CHECK(t.polys[0].flags == 0x01);

    // Detour (rx,ry,rz) -> world (rz,rx,ry): vert 2 (10,0,10) -> (10,10,0).
    Vec3 w = navToWorld(t.verts[2]);
    CHECK_APPROX(w.x, 10.0f); CHECK_APPROX(w.y, 10.0f); CHECK_APPROX(w.z, 0.0f);

    // addNavMesh draws the quad: 4 edges -> 8 line vertices.
    DebugDraw dd;
    addNavMesh(dd, t, Rgba{0, 255, 0, 255}, DebugCategory::NavMesh);
    CHECK(dd.categoryBuffers(DebugCategory::NavMesh).lines.size() == 4u * 2u);
}

void test_vmap() {
    std::printf("[vmap]\n");

    std::vector<uint8_t> buf;
    for (char c : std::string("VMAP_4.0")) buf.push_back((uint8_t)c);   // 8-byte magic
    praw(buf, "WMOD"); p32(buf, 8); p32(buf, 1234);   // chunkSize, rootWmoId

    // group meta (AABox 6 floats + flags + wmoId = 32 bytes) precedes VERT.
    pf(buf,-1); pf(buf,-1); pf(buf,-1); pf(buf,2); pf(buf,2); pf(buf,0);
    p32(buf, 0x4); p32(buf, 7);

    // VERT: 4 verts (a quad on z=0).
    praw(buf, "VERT"); p32(buf, 4 + 12*4); p32(buf, 4);
    pf(buf,0);pf(buf,0);pf(buf,0);  pf(buf,1);pf(buf,0);pf(buf,0);
    pf(buf,1);pf(buf,1);pf(buf,0);  pf(buf,0);pf(buf,1);pf(buf,0);
    // TRIM: 2 triangles.
    praw(buf, "TRIM"); p32(buf, 4 + 12*2); p32(buf, 2);
    p32(buf,0); p32(buf,1); p32(buf,2);
    p32(buf,0); p32(buf,2); p32(buf,3);

    VmapModel m = parseWorldModel(buf);
    CHECK(m.rootWmoId == 1234);
    CHECK(m.groups.size() == 1);
    const VmapGroup& g = m.groups[0];
    CHECK(g.flags == 0x4 && g.wmoId == 7);
    CHECK_APPROX(g.bmax.x, 2.0f);
    CHECK(g.vertices.size() == 4);
    CHECK(g.triangles.size() == 2);
    CHECK(g.triangles[1][1] == 2u);
    CHECK_APPROX(g.vertices[2].x, 1.0f);

    Mesh mesh = vmapGroupToMesh(g);
    CHECK(mesh.vertices.size() == 4 && mesh.indices.size() == 6);

    DebugDraw dd;
    addCollision(dd, m, Rgba{255,0,0,255});
    // 2 triangles * 3 edges * 2 verts = 12 line vertices.
    CHECK(dd.categoryBuffers(DebugCategory::Collision).lines.size() == 12u);
}

void test_storage() {
    std::printf("[storage]\n");

    // --- DBC builder round-trips through Dbc::parse -------------------------
    DbcBuilder b(5);
    uint32_t off = b.addString("Azeroth");
    uint32_t off2 = b.addString("Azeroth");          // dedup -> same offset
    CHECK(off == off2 && off != 0);
    b.addRecord({ 0, off, 2, 0, 0 });
    b.addRecord({ 1, b.addString("Kalimdor"), 0, 0, 0 });
    std::vector<uint8_t> blob = b.build();

    Dbc dbc = Dbc::parse(blob);
    CHECK(dbc.recordCount() == 2 && dbc.fieldCount() == 5);
    CHECK(dbc.getU32(0, 0) == 0 && dbc.getU32(0, 2) == 2);
    CHECK(dbc.getString(0, 1) == "Azeroth");
    CHECK(dbc.getString(1, 1) == "Kalimdor");
    CHECK(dbc.getString(0, 4) == "");                // offset 0 -> empty

    // The typed view reads the built record.
    MapEntry me = mapEntry(dbc, 1);
    CHECK(me.id == 1);

    // --- patch-MPQ writer: write, reopen read-only, verify byte-identical ---
    const char* path = "wforge_test_patch.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "DBFilesClient\\Map.dbc", blob },
        { "Custom\\hello.txt", std::vector<uint8_t>{ 'h','i',0 } },
    };
    bool wrote = writeMpqArchive(path, files);
    CHECK(wrote);
    if (wrote) {
        MpqManager mgr;
        CHECK(mgr.addArchive(path));
        CHECK(mgr.contains("DBFilesClient\\Map.dbc"));
        std::vector<uint8_t> back;
        CHECK(mgr.readFile("DBFilesClient\\Map.dbc", back));
        CHECK(back == blob);                          // exact round-trip
        std::vector<uint8_t> txt;
        CHECK(mgr.readFile("Custom\\hello.txt", txt));
        CHECK(txt.size() == 3 && txt[0] == 'h');
    }
    std::remove(path);
}
