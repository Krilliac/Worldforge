#include "test.hpp"
#include "m2.hpp"
#include "m2_render.hpp"
#include "bounds.hpp"

#include <cstring>
#include <vector>

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void putf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(b,v); }
void align4(std::vector<uint8_t>& b){ while (b.size() % 4) b.push_back(0); }
void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v){ for(int i=0;i<4;i++) b[at+i]=(v>>(8*i))&0xFF; }
} // namespace

void test_m2() {
    std::printf("[m2]\n");

    std::vector<uint8_t> f(0x150, 0);   // header region (zeroed)
    f[0]='M'; f[1]='D'; f[2]='2'; f[3]='0';
    patch32(f, 0x004, 0x100);           // version 256 (vanilla)

    // name block
    uint32_t nameOff = (uint32_t)f.size();
    for (char c : std::string("TestModel")) f.push_back((uint8_t)c);
    f.push_back(0);
    align4(f);

    // vertices: 3 verts, positions (i,i*2,i*3), uv (i*0.1, i*0.2)
    uint32_t vtxOff = (uint32_t)f.size();
    for (int i = 0; i < 3; ++i) {
        putf(f, (float)i); putf(f, (float)(i*2)); putf(f, (float)(i*3));  // pos
        for (int k=0;k<4;k++) f.push_back(k==0?255:0);   // bone weights
        for (int k=0;k<4;k++) f.push_back(0);            // bone indices
        putf(f, 0.f); putf(f, 0.f); putf(f, 1.f);        // normal up
        putf(f, i*0.1f); putf(f, i*0.2f);                // uv
        // M2Vertex is 48 bytes: 12+4+4+12+8 = 40, plus a 2nd uv set (8) = 48.
        putf(f, 0.f); putf(f, 0.f);                      // uv set 2
    }

    // texture name + record
    uint32_t texNameOff = (uint32_t)f.size();
    for (char c : std::string("World\\Tex.blp")) f.push_back((uint8_t)c);
    f.push_back(0); align4(f);
    uint32_t texOff = (uint32_t)f.size();
    put32(f, 0);                 // type 0 (hardcoded path)
    put32(f, 0);                 // flags
    put32(f, 14);                // name count (incl. null)
    put32(f, texNameOff);        // name offset

    // view 0 sub-arrays
    uint32_t lookupOff = (uint32_t)f.size();
    put16(f,0); put16(f,1); put16(f,2);          // vertex lookup
    uint32_t triOff = (uint32_t)f.size();
    put16(f,0); put16(f,1); put16(f,2);          // triangle indices
    uint32_t subOff = (uint32_t)f.size();
    put16(f,5); put16(f,0);                      // id=5, Level=0
    put16(f,0); put16(f,3);                      // vertexStart=0, vertexCount=3
    put16(f,0); put16(f,3);                      // indexStart=0, indexCount=3
    for (int i=0;i<20;i++) f.push_back(0);       // pad to 32-byte stride

    // view header (44 bytes): vertices, indices, bones, submeshes, batches, boneCountMax
    uint32_t viewOff = (uint32_t)f.size();
    put32(f,3); put32(f,lookupOff);              // vertices (lookup)
    put32(f,3); put32(f,triOff);                 // indices (triangles)
    put32(f,0); put32(f,0);                      // bones
    put32(f,1); put32(f,subOff);                 // submeshes
    put32(f,0); put32(f,0);                      // batches
    put32(f,0);                                  // boneCountMax

    // attachment record (48 bytes): id, bone, pos, then a 28-byte M2Track (skipped)
    uint32_t attachOff = (uint32_t)f.size();
    put32(f, 7);                 // id
    put32(f, 2);                 // bone
    putf(f, 1.5f); putf(f, 2.5f); putf(f, 3.5f);   // position
    for (int i=0;i<28;i++) f.push_back(0);         // animate-attached M2Track

    // light record (212 bytes): type(u16), bone(i16), pos, then 7 M2Tracks (skipped)
    uint32_t lightOff = (uint32_t)f.size();
    put16(f, 1);                 // type = point
    put16(f, (uint16_t)(int16_t)4);   // bone = 4
    putf(f, -1.0f); putf(f, 0.0f); putf(f, 9.0f);  // position
    for (int i=0;i<7*28;i++) f.push_back(0);       // colour/intensity/atten M2Tracks

    // camera record (124 bytes): type,fov,far,near, M2Track, pos, then the rest (skipped)
    uint32_t cameraOff = (uint32_t)f.size();
    put32(f, 0);                 // type = portrait
    putf(f, 0.95f);              // fov
    putf(f, 1000.0f);            // farClip
    putf(f, 0.1f);               // nearClip
    for (int i=0;i<28;i++) f.push_back(0);         // positions M2Track
    putf(f, 4.0f); putf(f, 5.0f); putf(f, 6.0f);   // static position
    for (int i=0;i<(124-16-28-12);i++) f.push_back(0); // target/roll tail (skipped)

    // ribbon-emitter record (0xB0 bytes): id, bone, pos, then AnimationBlocks (skipped)
    uint32_t ribbonOff = (uint32_t)f.size();
    put32(f, 11);                // id
    put32(f, (uint32_t)(int32_t)3);   // bone = 3
    putf(f, 0.5f); putf(f, 1.0f); putf(f, 2.0f);   // position
    for (int i=0;i<(0xB0-20);i++) f.push_back(0);  // embedded AnimationBlocks

    // particle-emitter record (0x1D8 bytes): id, bone, pos, then AnimationBlocks (skipped)
    uint32_t particleOff = (uint32_t)f.size();
    put32(f, 22);                // id
    put32(f, (uint32_t)(int32_t)6);   // bone = 6
    putf(f, 7.0f); putf(f, 8.0f); putf(f, 9.0f);   // position
    for (int i=0;i<(0x1D8-20);i++) f.push_back(0); // embedded AnimationBlocks

    // patch header arrays
    patch32(f, 0x008, 10);        patch32(f, 0x00C, nameOff);   // name
    patch32(f, 0x044, 3);         patch32(f, 0x048, vtxOff);    // vertices
    patch32(f, 0x04C, 1);         patch32(f, 0x050, viewOff);   // views
    patch32(f, 0x05C, 1);         patch32(f, 0x060, texOff);    // textures
    patch32(f, 0x0B4, 1);         patch32(f, 0x0B8, attachOff); // attachments
    patch32(f, 0x0CC, 1);         patch32(f, 0x0D0, lightOff);  // lights
    patch32(f, 0x0D4, 1);         patch32(f, 0x0D8, cameraOff); // cameras
    patch32(f, 0x0E4, 1);         patch32(f, 0x0E8, ribbonOff);   // ribbon emitters
    patch32(f, 0x0EC, 1);         patch32(f, 0x0F0, particleOff); // particle emitters

    // --- parse and verify ---
    M2Model m = parseM2(f);
    CHECK(m.version == 0x100);
    CHECK(m.name == "TestModel");
    CHECK(m.vertices.size() == 3);
    CHECK_APPROX(m.vertices[2].pos.x, 2.0f);
    CHECK_APPROX(m.vertices[2].pos.y, 4.0f);
    CHECK_APPROX(m.vertices[2].pos.z, 6.0f);
    CHECK_APPROX(m.vertices[1].uv.x, 0.1f);
    CHECK_APPROX(m.vertices[0].normal.z, 1.0f);
    CHECK(m.textures.size() == 1);
    CHECK(m.textures[0] == "World\\Tex.blp");
    CHECK(m.vertexLookup.size() == 3);
    CHECK(m.triangles.size() == 3);
    CHECK(m.submeshes.size() == 1);
    CHECK(m.submeshes[0].id == 5);
    CHECK(m.submeshes[0].vertexCount == 3 && m.submeshes[0].indexCount == 3);

    // The resolve chain (triangle -> lookup -> global vertex) is consistent.
    CHECK(m.resolveVertex(m.triangles[2]) == 2);

    // ---- attachments / cameras / lights (static leading fields) --------------
    CHECK(m.attachments.size() == 1);
    CHECK(m.attachments[0].id == 7);
    CHECK(m.attachments[0].bone == 2);
    CHECK_APPROX(m.attachments[0].position.x, 1.5f);
    CHECK_APPROX(m.attachments[0].position.z, 3.5f);

    CHECK(m.lights.size() == 1);
    CHECK(m.lights[0].type == 1);
    CHECK(m.lights[0].bone == 4);
    CHECK_APPROX(m.lights[0].position.x, -1.0f);
    CHECK_APPROX(m.lights[0].position.z, 9.0f);

    CHECK(m.cameras.size() == 1);
    CHECK(m.cameras[0].type == 0);
    CHECK_APPROX(m.cameras[0].fov, 0.95f);
    CHECK_APPROX(m.cameras[0].farClip, 1000.0f);
    CHECK_APPROX(m.cameras[0].nearClip, 0.1f);
    CHECK_APPROX(m.cameras[0].position.x, 4.0f);
    CHECK_APPROX(m.cameras[0].position.z, 6.0f);

    // ---- ribbon / particle emitters (static leading fields) ------------------
    CHECK(m.ribbonEmitters.size() == 1);
    CHECK(m.ribbonEmitters[0].id == 11);
    CHECK(m.ribbonEmitters[0].bone == 3);
    CHECK_APPROX(m.ribbonEmitters[0].position.x, 0.5f);
    CHECK_APPROX(m.ribbonEmitters[0].position.z, 2.0f);

    CHECK(m.particleEmitters.size() == 1);
    CHECK(m.particleEmitters[0].id == 22);
    CHECK(m.particleEmitters[0].bone == 6);
    CHECK_APPROX(m.particleEmitters[0].position.x, 7.0f);
    CHECK_APPROX(m.particleEmitters[0].position.z, 9.0f);

    // The additive path must no-op on a model lacking these arrays: a copy with
    // the attachment/light/camera header counts cleared parses with empty vectors
    // and is otherwise identical to the full model.
    {
        std::vector<uint8_t> noExtras = f;
        patch32(noExtras, 0x0B4, 0);   // attachments count = 0
        patch32(noExtras, 0x0CC, 0);   // lights count = 0
        patch32(noExtras, 0x0D4, 0);   // cameras count = 0
        patch32(noExtras, 0x0E4, 0);   // ribbon emitters count = 0
        patch32(noExtras, 0x0EC, 0);   // particle emitters count = 0
        M2Model m2 = parseM2(noExtras);
        CHECK(m2.attachments.empty());
        CHECK(m2.lights.empty());
        CHECK(m2.cameras.empty());
        CHECK(m2.ribbonEmitters.empty());
        CHECK(m2.particleEmitters.empty());
        CHECK(m2.vertices.size() == 3);   // the rest of the model is unaffected
    }

    // Every submesh draw range must stay inside the view's lookup/triangle lists
    // (an out-of-range start/count is the classic "exploded mesh" symptom on a
    // wrong stride). This mirrors the real-data sanity check in wforge-m2dump.
    for (const M2Submesh& s : m.submeshes) {
        CHECK(size_t(s.vertexStart) + s.vertexCount <= m.vertexLookup.size());
        CHECK(size_t(s.indexStart)  + s.indexCount  <= m.triangles.size());
    }

    // ---- skinning + bounds (the bind-pose path the M2 render tool exercises) ----
    // With an empty pose, skinM2 yields the static bind pose: one TexMesh vertex
    // per lookup entry, a multiple-of-3 index count, and every index in range.
    TexMesh mesh = skinM2(m, {});
    CHECK(mesh.vertices.size() == m.vertexLookup.size());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(mesh.indices.size() == m.triangles.size());
    for (uint32_t idx : mesh.indices) CHECK(idx < mesh.vertices.size());
    // The skinned positions follow the global vertices through the lookup.
    CHECK_APPROX(mesh.vertices[2].position.x, 2.0f);   // lookup[2] -> global vtx 2
    CHECK_APPROX(mesh.vertices[2].position.z, 6.0f);

    // Model-local bounds are non-degenerate and enclose the bind-pose vertices.
    Aabb box = modelBounds(m);
    CHECK(box.valid());
    CHECK(box.radius() > 0.0f);
    CHECK_APPROX(box.min.x, 0.0f);   // verts span x in [0,2], y [0,4], z [0,6]
    CHECK_APPROX(box.max.z, 6.0f);

    // --- ClientProfile version gate -------------------------------------------
    // A newer M2 (version 0x104, TBC) must fail loud under the vanilla profile,
    // but parse under a profile that allows that version.
    {
        std::vector<uint8_t> tbc = f;
        patch32(tbc, 0x004, 0x104);
        bool threw = false;
        try { parseM2(tbc); } catch (const std::exception&) { threw = true; }
        CHECK(threw);                                  // default (vanilla) profile rejects 0x104

        ClientProfile prof = vanilla1121Profile();
        prof.m2Version = 0x104;
        bool ok = true;
        try { (void)parseM2(tbc, prof); } catch (...) { ok = false; }
        CHECK(ok);                                     // a profile allowing 0x104 parses it

        CHECK(parseM2(f).version == 0x100);            // vanilla model still parses by default
    }
}
