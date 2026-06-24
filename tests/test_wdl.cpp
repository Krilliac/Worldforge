#include "test.hpp"
#include "wdl.hpp"

#include <cstdint>
#include <vector>

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
void magic(std::vector<uint8_t>& b, const char* m) { b.push_back(m[3]); b.push_back(m[2]); b.push_back(m[1]); b.push_back(m[0]); }
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p) {
    magic(b, m); put32(b, (uint32_t)p.size()); b.insert(b.end(), p.begin(), p.end());
}
}  // namespace

void test_wdl() {
    std::printf("[wdl]\n");

    // One present tile at (tx=5, ty=3) -> MAOF index 3*64+5 = 197.
    const int tx = 5, ty = 3, idx = ty * 64 + tx;

    // MARE payload: 289 outer (value == index) + 256 inner (zero).
    std::vector<uint8_t> mare;
    for (int i = 0; i < Wdl::N; ++i) put16(mare, (uint16_t)(int16_t)i);
    for (int i = 0; i < 16 * 16; ++i) put16(mare, 0);

    // Assemble: MVER, MAOF (patched after we know the MARE offset), MARE.
    std::vector<uint8_t> mver; put32(mver, 18);
    std::vector<uint8_t> file;
    chunk(file, "MVER", mver);

    // MAOF chunk: 4096 offsets, all zero for now (patched below).
    std::vector<uint8_t> maof(64 * 64 * 4, 0);
    const size_t maofChunkStart = file.size();        // where 'MAOF' magic goes
    chunk(file, "MAOF", maof);
    const size_t mareStart = file.size();             // MARE chunk start (magic)
    chunk(file, "MARE", mare);

    // Patch MAOF[idx] = mareStart. MAOF data begins at maofChunkStart + 8.
    const size_t entry = maofChunkStart + 8 + (size_t)idx * 4;
    for (int i = 0; i < 4; ++i) file[entry + i] = (uint8_t)((mareStart >> (8 * i)) & 0xFF);

    Wdl w = parseWdl(file);
    CHECK(w.tileCount() == 1);
    CHECK(w.tilePresent(tx, ty));
    CHECK(!w.tilePresent(0, 0));
    CHECK(!w.tilePresent(tx + 1, ty));
    // outer value == flat index: height(.,0,0)=0, (.,1,0)=row1col0=17, (.,0,1)=1.
    CHECK(w.height(tx, ty, 0, 0) == 0);
    CHECK(w.height(tx, ty, 1, 0) == 17);
    CHECK(w.height(tx, ty, 0, 1) == 1);
    CHECK(w.height(tx, ty, 16, 16) == Wdl::N - 1);   // 288

    // A buffer with no MAOF parses to an empty (all-absent) heightfield.
    std::vector<uint8_t> empty; chunk(empty, "MVER", mver);
    Wdl e = parseWdl(empty);
    CHECK(e.tileCount() == 0);
}
