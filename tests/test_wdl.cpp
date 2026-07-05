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

    // --- two present tiles at distinct indices, with gaps absent between them ---
    {
        const int ax = 5, ay = 3, bx = 10, by = 20;
        std::vector<uint8_t> mA, mB;
        for (int i = 0; i < Wdl::N; ++i) put16(mA, (uint16_t)(int16_t)i);          // 0..288
        for (int i = 0; i < 16 * 16; ++i) put16(mA, 0);
        for (int i = 0; i < Wdl::N; ++i) put16(mB, (uint16_t)(int16_t)(1000 + i));  // 1000..
        for (int i = 0; i < 16 * 16; ++i) put16(mB, 0);

        std::vector<uint8_t> f2;
        chunk(f2, "MVER", mver);
        std::vector<uint8_t> maof2(64 * 64 * 4, 0);
        const size_t maofStart = f2.size();
        chunk(f2, "MAOF", maof2);
        const size_t aStart = f2.size(); chunk(f2, "MARE", mA);
        const size_t bStart = f2.size(); chunk(f2, "MARE", mB);
        auto patch = [&](int idx, size_t v) { for (int i = 0; i < 4; ++i) f2[maofStart + 8 + (size_t)idx * 4 + i] = (uint8_t)((v >> (8 * i)) & 0xFF); };
        patch(ay * 64 + ax, aStart);
        patch(by * 64 + bx, bStart);

        Wdl w2 = parseWdl(f2);
        CHECK(w2.tileCount() == 2);
        CHECK(w2.tilePresent(ax, ay) && w2.tilePresent(bx, by));
        CHECK(!w2.tilePresent(ax, by) && !w2.tilePresent(bx, ay));   // gaps absent
        CHECK(w2.height(ax, ay, 0, 0) == 0);
        CHECK(w2.height(bx, by, 0, 0) == 1000);
        CHECK(w2.height(bx, by, 1, 0) == 1017);                     // row1col0 = 1000+17
    }

    // --- no MAHO (pre-WotLK writer): zero holes, offset 0, patcher skips ---
    {
        CHECK(!wdlChunkIsHole(w, tx, ty, 3, 5));
        CHECK(w.mahoOffset[idx] == 0);
        Wdl edited = w;
        setWdlHole(edited, tx, ty, 3, 5, true);                     // edit sticks in memory...
        CHECK(wdlChunkIsHole(edited, tx, ty, 3, 5));
        std::vector<uint8_t> out = writeWdlHoles(file, edited);     // ...but there is no chunk
        CHECK(out == file);                                         // to patch: byte-identical
    }

    // --- MAHO: parse, query, edit, patch back, re-parse ---
    {
        std::vector<uint8_t> mare2;
        for (int i = 0; i < Wdl::N; ++i) put16(mare2, (uint16_t)(int16_t)i);
        for (int i = 0; i < 16 * 16; ++i) put16(mare2, 0);
        std::vector<uint8_t> maho(32, 0);
        maho[3 * 2] = 1 << 5;                       // row 3, col 5 holed (LE low byte)

        std::vector<uint8_t> f;
        chunk(f, "MVER", mver);
        std::vector<uint8_t> maofh(64 * 64 * 4, 0);
        const size_t maofStart = f.size();
        chunk(f, "MAOF", maofh);
        const size_t mareStart2 = f.size();
        chunk(f, "MARE", mare2);
        chunk(f, "MAHO", maho);
        for (int i = 0; i < 4; ++i) f[maofStart + 8 + (size_t)idx * 4 + i] = (uint8_t)((mareStart2 >> (8 * i)) & 0xFF);

        Wdl h = parseWdl(f);
        CHECK(h.tilePresent(tx, ty));
        // MAHO payload = MARE start + header + payload + MAHO header.
        const size_t mahoPayload = mareStart2 + 8 + mare2.size() + 8;
        CHECK(h.mahoOffset[idx] == mahoPayload);
        CHECK(wdlChunkIsHole(h, tx, ty, 3, 5));
        bool othersClear = true;                    // every other bit reads clear
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                if (!(r == 3 && c == 5) && wdlChunkIsHole(h, tx, ty, r, c)) othersClear = false;
        CHECK(othersClear);
        // Out-of-range row/col and absent tiles read as "not a hole".
        CHECK(!wdlChunkIsHole(h, tx, ty, -1, 5));
        CHECK(!wdlChunkIsHole(h, tx, ty, 3, 16));
        CHECK(!wdlChunkIsHole(h, 63, 63, 3, 5));

        // Edit: clear (3,5), set (0,0) and (15,15); out-of-range edits ignored.
        setWdlHole(h, tx, ty, 3, 5, false);
        setWdlHole(h, tx, ty, 0, 0, true);
        setWdlHole(h, tx, ty, 15, 15, true);
        setWdlHole(h, tx, ty, -1, 0, true);
        setWdlHole(h, tx, ty, 0, 16, true);
        std::vector<uint8_t> out = writeWdlHoles(f, h);
        CHECK(out.size() == f.size());
        bool outsideSame = true;                    // only the 32 MAHO bytes may differ
        for (size_t i = 0; i < f.size(); ++i)
            if ((i < mahoPayload || i >= mahoPayload + 32) && out[i] != f[i]) outsideSame = false;
        CHECK(outsideSame);

        Wdl h2 = parseWdl(out);                     // re-parse round-trips the edits
        CHECK(h2.holes == h.holes);
        CHECK(!wdlChunkIsHole(h2, tx, ty, 3, 5));
        CHECK(wdlChunkIsHole(h2, tx, ty, 0, 0));
        CHECK(wdlChunkIsHole(h2, tx, ty, 15, 15));
    }

    // --- ADT->WDL sync rule: WDL bit set iff the chunk is FULLY holed ---
    {
        CHECK(adtChunkFullyHoled(0xFFFF));
        CHECK(!adtChunkFullyHoled(0xFFFE));
        CHECK(!adtChunkFullyHoled(0x0000));

        Wdl s;                                      // hand-built, mask unallocated
        CHECK(!wdlChunkIsHole(s, 2, 2, 4, 4));
        syncWdlFromAdt(s, 2, 2, 4, 4, 0xFFFF);      // fully holed -> set
        CHECK(wdlChunkIsHole(s, 2, 2, 4, 4));
        syncWdlFromAdt(s, 2, 2, 4, 4, 0xFFFE);      // one bit intact -> clear
        CHECK(!wdlChunkIsHole(s, 2, 2, 4, 4));
    }
}
