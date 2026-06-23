#include "test.hpp"
#include "m2.hpp"

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

    // patch header arrays
    patch32(f, 0x008, 10);        patch32(f, 0x00C, nameOff);   // name
    patch32(f, 0x044, 3);         patch32(f, 0x048, vtxOff);    // vertices
    patch32(f, 0x04C, 1);         patch32(f, 0x050, viewOff);   // views
    patch32(f, 0x05C, 1);         patch32(f, 0x060, texOff);    // textures

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
}
