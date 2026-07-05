#include "test.hpp"
#include "gridmap.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace wf;

namespace {

// Sinusoidal V9/V8 height field spanning roughly `span` yards around `base`.
void makeField(std::vector<float>& v9, std::vector<float>& v8, float base, float span) {
    v9.resize(GRIDMAP_V9 * GRIDMAP_V9);
    v8.resize(GRIDMAP_V8 * GRIDMAP_V8);
    const float amp = span * 0.5f;
    for (int y = 0; y < GRIDMAP_V9; ++y)
        for (int x = 0; x < GRIDMAP_V9; ++x)
            v9[y * GRIDMAP_V9 + x] = base + amp * std::sin(x * 0.13f) * std::cos(y * 0.11f);
    for (int y = 0; y < GRIDMAP_V8; ++y)
        for (int x = 0; x < GRIDMAP_V8; ++x)
            v8[y * GRIDMAP_V8 + x] = base + amp * std::sin((x + 0.5f) * 0.13f) * std::cos((y + 0.5f) * 0.11f);
}

float maxAbsErr(const std::vector<float>& a, const std::vector<float>& b) {
    float e = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        e = (std::fabs(a[i] - b[i]) > e) ? std::fabs(a[i] - b[i]) : e;
    return e;
}

bool parseThrows(const std::vector<uint8_t>& buf) {
    try { (void)parseGridMap(buf); } catch (const std::exception&) { return true; }
    return false;
}

} // namespace

void test_gridmap_writer() {
    std::printf("[gridmap_writer]\n");

    // Shared fixtures: varied area ids, a couple of holes, a typed liquid
    // with a non-uniform 4x5 surface.
    std::array<uint16_t, 16 * 16> areas{};
    for (int i = 0; i < 16 * 16; ++i) areas[i] = (uint16_t)(5 + (i % 7) * 3);
    std::array<uint16_t, 16 * 16> holes{};
    holes[5] = 0x00FF;
    holes[200] = 0x8001;
    std::array<uint16_t, 16 * 16> holesNone{};

    GridMapLiquid lq;
    lq.liquidType = 8;
    lq.offsetX = 2; lq.offsetY = 3; lq.width = 4; lq.height = 5;
    lq.liquidLevel = 42.5f;
    lq.present = true;
    lq.entry.resize(16 * 16);
    lq.liquidFlags.resize(16 * 16);
    for (int i = 0; i < 16 * 16; ++i) {
        lq.entry[i] = (uint16_t)(i % 3 ? 8 : 2);
        lq.liquidFlags[i] = (uint8_t)(i % 2);
    }
    lq.heightMap.resize(4 * 5);
    for (int i = 0; i < 4 * 5; ++i) lq.heightMap[i] = 42.0f + 0.1f * i;

    // --- (1) round-trip, quantization disabled: bit-exact heights ---
    std::vector<float> v9, v8;
    makeField(v9, v8, 30.0f, 40.0f);
    GridMapData src = makeGridMapData(v9, v8, 0.0f, areas, holes, &lq);
    src.versionMagic = 0x352e317au;   // 'z1.5'
    src.buildMagic   = 5875;

    GridMapWriteOptions exact;
    exact.allowQuantize = false;
    const std::vector<uint8_t> bufExact = writeGridMap(src, exact);

    GridMap m = parseGridMap(bufExact);
    CHECK(m.versionMagic == 0x352e317au && m.buildMagic == 5875);
    CHECK(m.hasArea && !m.area.uniform);
    CHECK(m.area.grid == areas);
    CHECK(m.hasHeightSection && m.height.present);
    CHECK(m.height.v9 == v9);                         // bit-exact float32 round-trip
    CHECK(m.height.v8 == v8);
    CHECK(m.hasLiquid && m.liquid.present);
    CHECK(m.liquid.entry == lq.entry);
    CHECK(m.liquid.liquidFlags == lq.liquidFlags);
    CHECK(m.liquid.heightMap == lq.heightMap);
    CHECK(m.liquid.offsetX == 2 && m.liquid.offsetY == 3);
    CHECK(m.liquid.width == 4 && m.liquid.height == 5);
    CHECK_APPROX(m.liquid.liquidLevel, 42.5f);
    CHECK(m.hasHoles && m.holes == holes);

    // --- (2) quantization error bounds ---
    // 2000yd span: u8 worst-case (2000/255/2 ~ 3.9) > 2.0, u16 (~0.015) <= 10
    // -> the u16 path is chosen and stays within half a quantization step.
    std::vector<float> w9, w8;
    makeField(w9, w8, 100.0f, 2000.0f);
    GridMapData big = makeGridMapData(w9, w8, 0.0f, areas, holes, nullptr);
    const std::vector<uint8_t> bufU16 = writeGridMap(big);   // default options
    GridMap mq = parseGridMap(bufU16);
    const float diffBig = big.height.gridMaxHeight - big.height.gridHeight;
    const float boundU16 = diffBig / 65535.0f * 0.5f + 1e-3f;
    CHECK(mq.height.present);
    CHECK(maxAbsErr(mq.height.v9, w9) <= boundU16);
    CHECK(maxAbsErr(mq.height.v8, w8) <= boundU16);
    CHECK(bufU16.size() < bufExact.size());                  // u16 = half the float payload

    // 100yd span: u8 worst-case (100/255/2 ~ 0.196) <= 2.0 -> u8 path, < 2.0.
    std::vector<float> s9, s8;
    makeField(s9, s8, 20.0f, 100.0f);
    GridMapData srcU8 = makeGridMapData(s9, s8, 0.0f, areas, holes, nullptr);
    const std::vector<uint8_t> bufU8 = writeGridMap(srcU8);
    GridMap m8 = parseGridMap(bufU8);
    const float diffSmall = srcU8.height.gridMaxHeight - srcU8.height.gridHeight;
    CHECK(m8.height.present);
    CHECK(maxAbsErr(m8.height.v9, s9) <= diffSmall / 255.0f * 0.5f + 1e-3f);
    CHECK(maxAbsErr(m8.height.v9, s9) < 2.0f);
    CHECK(maxAbsErr(m8.height.v8, s8) < 2.0f);
    CHECK(bufU8.size() < bufU16.size());                     // u8 = half the u16 payload

    // --- (3) flat grid: NO_HEIGHT flag + tiny buffer ---
    GridMapData flat = makeGridMapData({}, {}, 64.25f, areas, holes, nullptr);
    const std::vector<uint8_t> bufFlat = writeGridMap(flat);
    GridMap mf = parseGridMap(bufFlat);
    CHECK(mf.hasHeightSection && !mf.height.present);        // NO_HEIGHT emitted
    CHECK_APPROX(mf.heightV9(50, 50), 64.25f);
    CHECK(!mf.hasLiquid);
    CHECK(mf.hasHoles && mf.holes == holes);                 // holesOfs correct with MLIQ omitted
    CHECK(bufFlat.size() < bufU8.size());                    // no height array at all
    CHECK(bufFlat.size() < 1200);                            // header + AREA + MHGT + holes

    // All-equal (non-empty) grids collapse to the same flat form; min == max
    // must not divide by zero in the quantizer.
    std::vector<float> f9(GRIDMAP_V9 * GRIDMAP_V9, 7.5f), f8(GRIDMAP_V8 * GRIDMAP_V8, 7.5f);
    GridMapData flat2 = makeGridMapData(f9, f8, 0.0f, areas, holesNone, nullptr);
    GridMap mf2 = parseGridMap(writeGridMap(flat2));
    CHECK(!mf2.height.present);
    CHECK_APPROX(mf2.heightV9(0, 0), 7.5f);
    CHECK(!mf2.hasHoles);                                    // all-zero holes -> section omitted

    // --- (4) uniform collapses: area id + liquid type/surface ---
    std::array<uint16_t, 16 * 16> areasU{};
    areasU.fill(77);
    GridMapLiquid ulq;
    ulq.liquidType = 0;                                      // uniform type comes from entry[0]
    ulq.offsetX = 0; ulq.offsetY = 0; ulq.width = 3; ulq.height = 3;
    ulq.liquidLevel = 0.0f;                                  // uniform level comes from heightMap[0]
    ulq.present = true;
    ulq.entry.assign(16 * 16, 8);
    ulq.liquidFlags.assign(16 * 16, 1);
    ulq.heightMap.assign(3 * 3, 35.0f);
    GridMapData uni = makeGridMapData(v9, v8, 0.0f, areasU, holesNone, &ulq);
    const std::vector<uint8_t> bufUni = writeGridMap(uni, exact);
    GridMap mu = parseGridMap(bufUni);
    CHECK(mu.hasArea && mu.area.uniform);                    // uniform flag set
    CHECK(mu.area.uniformArea == 77);
    CHECK(mu.hasLiquid && !mu.liquid.present);               // surface collapsed to level
    CHECK(mu.liquid.entry.empty());                          // type grid collapsed
    CHECK(mu.liquid.liquidType == 8);
    CHECK_APPROX(mu.liquid.liquidLevel, 35.0f);
    CHECK(bufUni.size() < bufExact.size());

    // --- (5) corrupted round-trip guard: the writer emits the exact magics ---
    std::vector<uint8_t> bad = bufExact;
    bad[0] ^= 0xFF;                                          // 'M' of "MAPS"
    CHECK(parseThrows(bad));
    std::vector<uint8_t> bad2 = bufExact;
    bad2[44] ^= 0xFF;                                        // 'A' of "AREA" (first section)
    CHECK(parseThrows(bad2));

    // NaN heights are rejected at write time.
    std::vector<float> n9 = v9;
    n9[10] = std::nanf("");
    GridMapData nanSrc = makeGridMapData(n9, v8, 0.0f, areas, holesNone, nullptr);
    bool threw = false;
    try { (void)writeGridMap(nanSrc); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
