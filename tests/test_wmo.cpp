#include "test.hpp"
#include "wmo.hpp"
#include "wmo_render.hpp"

#include <cstring>
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
void cstr(std::vector<uint8_t>& b, const char* s){ for(const char* p=s;*p;++p) b.push_back(*p); b.push_back(0); }
} // namespace

void test_wmo() {
    std::printf("[wmo]\n");

    // ---------------- root ----------------
    std::vector<uint8_t> root;

    std::vector<uint8_t> mohd;
    put32(mohd, 2);   // nTextures
    put32(mohd, 1);   // nGroups
    put32(mohd, 0);   // nPortals
    put32(mohd, 0);   // nLights
    put32(mohd, 1);   // nDoodadNames
    put32(mohd, 1);   // nDoodadDefs
    put32(mohd, 1);   // nDoodadSets
    put32(mohd, 0);   // ambColor
    put32(mohd, 99);  // wmoID
    putf(mohd,-10);putf(mohd,-20);putf(mohd,-30);   // bbox min
    putf(mohd, 10);putf(mohd, 20);putf(mohd, 30);   // bbox max
    put16(mohd, 0);   // flags
    put16(mohd, 0);   // numLod
    chunk(root, "MOHD", mohd);

    // MOTX: two texture names, 4-byte aligned.
    std::vector<uint8_t> motx;
    cstr(motx, "A.blp");              // offset 0
    while (motx.size() % 4) motx.push_back(0);
    uint32_t tex2Off = (uint32_t)motx.size();
    cstr(motx, "B.blp");
    while (motx.size() % 4) motx.push_back(0);
    chunk(root, "MOTX", motx);

    // MOMT: two materials, second references B.blp.
    std::vector<uint8_t> momt;
    auto mat = [&](uint32_t diffOff){
        put32(momt,0); put32(momt,0); put32(momt,0); put32(momt,diffOff);
        for(int i=0;i<12;i++) put32(momt,0);
    };
    mat(0); mat(tex2Off);
    chunk(root, "MOMT", momt);

    // MOGN: group name "main".
    std::vector<uint8_t> mogn;
    mogn.push_back(0); mogn.push_back(0);   // wiki: begins with two empty strings
    uint32_t gnameOff = (uint32_t)mogn.size();
    cstr(mogn, "main");
    chunk(root, "MOGN", mogn);

    // MOGI: one group.
    std::vector<uint8_t> mogi;
    put32(mogi, 0x8);                          // flags (EXTERIOR)
    putf(mogi,-1);putf(mogi,-2);putf(mogi,-3);
    putf(mogi, 1);putf(mogi, 2);putf(mogi, 3);
    put32(mogi, gnameOff);                     // name offset
    chunk(root, "MOGI", mogi);

    // MODS: one doodad set.
    std::vector<uint8_t> mods;
    { const char* nm = "Set_$DefaultGlobal";
      for (int i=0;i<20;i++) mods.push_back(i < (int)std::strlen(nm) ? nm[i] : 0); }
    put32(mods, 0);   // firstInstance
    put32(mods, 1);   // numDoodads
    put32(mods, 0);   // unused
    chunk(root, "MODS", mods);

    // MODN: doodad model name.
    std::vector<uint8_t> modnBlob;
    cstr(modnBlob, "World\\Chair.mdx");
    chunk(root, "MODN", modnBlob);

    // MODD: one doodad instance, identity quaternion.
    std::vector<uint8_t> modd;
    put32(modd, 0);                  // nameOffset (low 24 bits) + flags
    putf(modd, 5); putf(modd, 6); putf(modd, 7);            // position
    putf(modd, 0); putf(modd, 0); putf(modd, 0); putf(modd, 1); // quat (x,y,z,w)
    putf(modd, 2.0f);                // scale
    put32(modd, 0xFFFFFFFF);         // color
    chunk(root, "MODD", modd);

    // MOLT: one interior light (48-byte SMOLight). MOHD nLights stays 0 above,
    // so a correct parse must count by chunk size, not the header field.
    std::vector<uint8_t> molt;
    molt.push_back(1);                       // type = spot
    molt.push_back(1);                       // useAttenuation
    molt.push_back(0); molt.push_back(0);    // padding
    molt.push_back(64); molt.push_back(128); molt.push_back(255); molt.push_back(255); // BGRA -> rgb(255,128,64)
    putf(molt, 3); putf(molt, 4); putf(molt, 5);   // position
    putf(molt, 1.5f);                        // intensity
    putf(molt, 2.0f);                        // attenStart
    putf(molt, 8.0f);                        // attenEnd
    for (int i = 0; i < 4; ++i) putf(molt, 0.0f);  // trailing unknowns
    chunk(root, "MOLT", molt);

    // MOSB: single zero-terminated skybox model path.
    std::vector<uint8_t> mosb;
    cstr(mosb, "Env\\Sky.mdx");
    chunk(root, "MOSB", mosb);

    // MOFG: one 48-byte SMOFog. Only the first of its two fog entries is read;
    // BGRA colour matches the MOLT convention -> rgb(255,128,64).
    std::vector<uint8_t> mofg;
    put32(mofg, 0x2);                        // flags
    putf(mofg, 1); putf(mofg, 2); putf(mofg, 3);   // position
    putf(mofg, 10.0f);                       // smallerRadius
    putf(mofg, 20.0f);                       // largerRadius
    putf(mofg, 100.0f);                      // end (first fog)
    putf(mofg, 0.25f);                       // startScalar
    mofg.push_back(64); mofg.push_back(128); mofg.push_back(255); mofg.push_back(255); // BGRA
    putf(mofg, 0.0f); putf(mofg, 0.0f); put32(mofg, 0);   // second fog entry (skipped)
    chunk(root, "MOFG", mofg);

    // MOPV: two portal vertices.
    std::vector<uint8_t> mopv;
    putf(mopv, 1); putf(mopv, 2); putf(mopv, 3);
    putf(mopv, 4); putf(mopv, 5); putf(mopv, 6);
    chunk(root, "MOPV", mopv);

    // MOPT: one 20-byte SMOPortal.
    std::vector<uint8_t> mopt;
    put16(mopt, 0);                          // startVertex
    put16(mopt, 2);                          // count
    putf(mopt, 0); putf(mopt, 0); putf(mopt, 1);   // normal
    putf(mopt, 7.5f);                        // planeDist
    chunk(root, "MOPT", mopt);

    WmoRoot wr = parseWmoRoot(root);
    CHECK(wr.nGroups == 1 && wr.nTextures == 2);
    CHECK_APPROX(wr.bboxMax.z, 30.0f);
    CHECK(wr.textures.size() == 2);
    CHECK(wr.textures[0] == "A.blp" && wr.textures[1] == "B.blp");
    CHECK(wr.materials.size() == 2);
    CHECK(wr.materials[1].diffuseTexture == "B.blp");
    CHECK(wr.groups.size() == 1);
    CHECK(wr.groups[0].name == "main");
    CHECK(wr.groups[0].flags == 0x8);
    CHECK(wr.doodadSets.size() == 1);
    CHECK(wr.doodadSets[0].name == "Set_$DefaultGlobal");
    CHECK(wr.doodads.size() == 1);
    CHECK(wr.doodads[0].modelName == "World\\Chair.mdx");
    CHECK_APPROX(wr.doodads[0].position.x, 5.0f);
    CHECK_APPROX(wr.doodads[0].orientation.w, 1.0f);
    CHECK_APPROX(wr.doodads[0].scale, 2.0f);
    CHECK(wr.lights.size() == 1);
    CHECK(wr.lights[0].type == 1 && wr.lights[0].useAttenuation);
    CHECK_APPROX(wr.lights[0].color.x, 1.0f);              // R = 255/255
    CHECK_APPROX(wr.lights[0].color.y, 128.0f / 255.0f);   // G
    CHECK_APPROX(wr.lights[0].color.z, 64.0f / 255.0f);    // B
    CHECK_APPROX(wr.lights[0].position.y, 4.0f);
    CHECK_APPROX(wr.lights[0].intensity, 1.5f);
    CHECK_APPROX(wr.lights[0].attenEnd, 8.0f);
    CHECK(wr.skybox == "Env\\Sky.mdx");
    CHECK(wr.fogs.size() == 1);
    CHECK(wr.fogs[0].flags == 0x2);
    CHECK_APPROX(wr.fogs[0].smallerRadius, 10.0f);
    CHECK_APPROX(wr.fogs[0].largerRadius, 20.0f);
    CHECK_APPROX(wr.fogs[0].end, 100.0f);
    CHECK_APPROX(wr.fogs[0].startScalar, 0.25f);
    CHECK_APPROX(wr.fogs[0].color.x, 1.0f);              // R = 255/255
    CHECK_APPROX(wr.fogs[0].color.y, 128.0f / 255.0f);   // G
    CHECK_APPROX(wr.fogs[0].color.z, 64.0f / 255.0f);    // B
    CHECK(wr.portalVertices.size() == 2);
    CHECK_APPROX(wr.portalVertices[1].x, 4.0f);
    CHECK(wr.portals.size() == 1);
    CHECK(wr.portals[0].startVertex == 0 && wr.portals[0].count == 2);
    CHECK_APPROX(wr.portals[0].normal.z, 1.0f);
    CHECK_APPROX(wr.portals[0].planeDist, 7.5f);

    // ---------------- group ----------------
    std::vector<uint8_t> group;

    // Build subchunks first.
    std::vector<uint8_t> movt;     // 3 vertices
    putf(movt,0);putf(movt,0);putf(movt,0);
    putf(movt,1);putf(movt,0);putf(movt,0);
    putf(movt,0);putf(movt,1);putf(movt,0);
    std::vector<uint8_t> monr;     // 3 normals up
    for(int i=0;i<3;i++){ putf(monr,0);putf(monr,0);putf(monr,1); }
    std::vector<uint8_t> motv;     // 3 uvs
    putf(motv,0);putf(motv,0); putf(motv,1);putf(motv,0); putf(motv,0);putf(motv,1);
    std::vector<uint8_t> movi;     // one triangle
    put16(movi,0); put16(movi,1); put16(movi,2);
    std::vector<uint8_t> mopy;     // one triangle material
    mopy.push_back(0); mopy.push_back(1);   // flags, materialId=1
    std::vector<uint8_t> moba;     // one batch
    for(int i=0;i<12;i++) moba.push_back(0);   // bbox
    put32(moba, 0);    // startIndex
    put16(moba, 3);    // indexCount
    put16(moba, 0);    // minIndex
    put16(moba, 2);    // maxIndex
    moba.push_back(0); // flags
    moba.push_back(1); // materialId
    std::vector<uint8_t> mobn;     // one collision BSP leaf node (16 bytes)
    put16(mobn, 4);    // flags (leaf)
    put16(mobn, 0xFFFF); // negChild = -1
    put16(mobn, 0xFFFF); // posChild = -1
    put16(mobn, 1);    // nFaces
    put32(mobn, 0);    // faceStart
    putf (mobn, 2.5f); // planeDist
    std::vector<uint8_t> mobv;     // two face indices into MOVI triangles
    put16(mobv, 0); put16(mobv, 1);
    std::vector<uint8_t> mocv;     // 3 vertex colors (BGRA), one per vertex
    mocv.push_back(64); mocv.push_back(128); mocv.push_back(255); mocv.push_back(255); // BGRA -> rgba(255,128,64,255)
    mocv.push_back(0);  mocv.push_back(0);   mocv.push_back(0);   mocv.push_back(255);
    mocv.push_back(255);mocv.push_back(255); mocv.push_back(255); mocv.push_back(255);

    // MOGP = 68-byte header + subchunks.
    std::vector<uint8_t> mogp(0x44, 0);
    { uint32_t fl = 0x8; for(int i=0;i<4;i++) mogp[0x08+i]=(fl>>(8*i))&0xFF; }   // flags
    auto append = [&](const char* m, const std::vector<uint8_t>& p){ chunk(mogp, m, p); };
    append("MOVT", movt);
    append("MONR", monr);
    append("MOTV", motv);
    append("MOVI", movi);
    append("MOPY", mopy);
    append("MOBA", moba);
    append("MOBN", mobn);
    append("MOBV", mobv);
    append("MOCV", mocv);
    chunk(group, "MOGP", mogp);

    WmoGroup wg = parseWmoGroup(group);
    CHECK(wg.flags == 0x8);
    CHECK(wg.vertices.size() == 3);
    CHECK_APPROX(wg.vertices[1].x, 1.0f);
    CHECK(wg.normals.size() == 3 && wg.uvs.size() == 3);
    CHECK(wg.indices.size() == 3);
    CHECK(wg.triMaterial.size() == 1 && wg.triMaterial[0] == 1);
    CHECK(wg.batches.size() == 1);
    CHECK(wg.batches[0].indexCount == 3 && wg.batches[0].materialId == 1);
    CHECK(wg.bspNodes.size() == 1);
    CHECK(wg.bspNodes[0].flags == 4 && wg.bspNodes[0].nFaces == 1);
    CHECK(wg.bspNodes[0].negChild == -1 && wg.bspNodes[0].posChild == -1);
    CHECK_APPROX(wg.bspNodes[0].planeDist, 2.5f);
    CHECK(wg.bspFaceIndices.size() == 2);
    CHECK(wg.bspFaceIndices[1] == 1);
    CHECK(wg.vertexColors.size() == 3);
    CHECK(wg.vertexColors[0].r == 255 && wg.vertexColors[0].g == 128 &&
          wg.vertexColors[0].b == 64  && wg.vertexColors[0].a == 255);

    // ---------------- render parts ----------------
    // The single triangle uses material 1 -> resolves to texture "B.blp", and
    // carries the position/normal/UV the textured rasteriser needs.
    WmoModel model; model.root = wr; model.groups.push_back(wg);
    std::vector<WmoRenderPart> parts = wmoRenderParts(model);
    CHECK(parts.size() == 1);
    CHECK(parts[0].texture == "B.blp");
    CHECK(parts[0].mesh.vertices.size() == 3 && parts[0].mesh.indices.size() == 3);
    CHECK_APPROX(parts[0].mesh.vertices[1].position.x, 1.0f);
    CHECK_APPROX(parts[0].mesh.vertices[0].normal.z, 1.0f);

    // Two materials, one alpha-blended: parts come back opaque-first, with the
    // material's blend mode + texture resolved.
    WmoModel mm;
    mm.root.materials.resize(2);
    mm.root.materials[0].blendMode = 0; mm.root.materials[0].diffuseTexture = "opaque.blp";
    mm.root.materials[1].blendMode = 3; mm.root.materials[1].diffuseTexture = "glass.blp";
    WmoGroup g2;
    g2.vertices = { {0,0,0},{1,0,0},{0,1,0}, {2,0,0},{3,0,0},{2,1,0} };
    g2.indices  = { 0,1,2, 3,4,5 };
    g2.triMaterial = { 1, 0 };                  // tri0 -> glass (blend), tri1 -> opaque
    mm.groups.push_back(g2);
    std::vector<WmoRenderPart> p2 = wmoRenderParts(mm);
    CHECK(p2.size() == 2);
    CHECK(p2[0].blendMode == 0 && p2[0].texture == "opaque.blp");   // opaque first
    CHECK(p2[1].blendMode == 3 && p2[1].texture == "glass.blp");    // blended last

    // ---------------- MOGP header bbox + 0xFF material id ----------------
    // The MOGP header carries the group bbox at 0x0C..0x23; make sure it's read.
    // And a triangle tagged material id 0xFF (collision-only, "no render") must
    // bucket separately with an out-of-range id resolving to an empty texture --
    // not silently merge into material 0. This mirrors what real WMO group files
    // contain (e.g. interior collision geometry).
    std::vector<uint8_t> g3;
    std::vector<uint8_t> v3;     // 6 verts -> 2 tris
    for (int i = 0; i < 6; ++i) { putf(v3, (float)i); putf(v3, 0); putf(v3, 0); }
    std::vector<uint8_t> i3;     // 2 triangles
    put16(i3,0);put16(i3,1);put16(i3,2); put16(i3,3);put16(i3,4);put16(i3,5);
    std::vector<uint8_t> p3;     // tri0 mat=0, tri1 mat=0xFF (collision)
    p3.push_back(0); p3.push_back(0);
    p3.push_back(0); p3.push_back(0xFF);
    std::vector<uint8_t> hdr(0x44, 0);
    { uint32_t fl = 0x4; for (int i=0;i<4;i++) hdr[0x08+i]=(fl>>(8*i))&0xFF; }   // flags
    auto putfAt = [&](int off, float f){ uint32_t u; std::memcpy(&u,&f,4);
                                         for(int i=0;i<4;i++) hdr[off+i]=(u>>(8*i))&0xFF; };
    putfAt(0x0C,-5);putfAt(0x10,-6);putfAt(0x14,-7);   // bbox min
    putfAt(0x18, 5);putfAt(0x1C, 6);putfAt(0x20, 7);   // bbox max
    auto app3 = [&](const char* m, const std::vector<uint8_t>& p){ chunk(hdr, m, p); };
    app3("MOVT", v3);
    app3("MOVI", i3);
    app3("MOPY", p3);
    chunk(g3, "MOGP", hdr);

    WmoGroup wg3 = parseWmoGroup(g3);
    CHECK(wg3.flags == 0x4);
    CHECK_APPROX(wg3.bboxMin.x, -5.0f);
    CHECK_APPROX(wg3.bboxMax.z,  7.0f);
    CHECK(wg3.indices.size() == 6 && wg3.triMaterial.size() == 2);
    CHECK(wg3.triMaterial[1] == 0xFF);

    WmoModel mm3; mm3.groups.push_back(wg3);
    mm3.root.materials.resize(1);                       // only material 0 exists
    mm3.root.materials[0].diffuseTexture = "wall.blp";
    std::vector<WmoRenderPart> p3parts = wmoRenderParts(mm3);
    CHECK(p3parts.size() == 2);                         // mat 0 + the 0xFF bucket
    // One part resolves to wall.blp; the 0xFF (out-of-range) part has no texture.
    bool sawWall = false, sawEmpty = false;
    for (const WmoRenderPart& p : p3parts) {
        if (p.texture == "wall.blp") sawWall = true;
        if (p.texture.empty())       sawEmpty = true;
        CHECK(p.mesh.indices.size() == 3);             // one triangle each
    }
    CHECK(sawWall && sawEmpty);

    // --- ClientProfile version gate -------------------------------------------
    // A minimal root with an MVER chunk: v17 parses, a newer version fails loud
    // under the vanilla profile but parses under a profile that allows it.
    {
        auto makeWmoVer = [](uint32_t ver) {
            std::vector<uint8_t> mver; put32(mver, ver);
            std::vector<uint8_t> mohd(64, 0);          // counts/bbox/flags, all zero
            std::vector<uint8_t> w;
            chunk(w, "MVER", mver);
            chunk(w, "MOHD", mohd);
            return w;
        };
        CHECK(parseWmoRoot(makeWmoVer(17)).version == 17);   // vanilla v17 parses

        bool threw = false;
        try { parseWmoRoot(makeWmoVer(18)); } catch (const std::exception&) { threw = true; }
        CHECK(threw);                                        // vanilla profile rejects v18

        ClientProfile prof = vanilla1121Profile();
        prof.wmoVersion = 18;
        bool ok = true;
        try { (void)parseWmoRoot(makeWmoVer(18), prof); } catch (...) { ok = false; }
        CHECK(ok);                                           // a profile allowing v18 parses it
    }
}
