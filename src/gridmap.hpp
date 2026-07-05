#pragma once
// ---------------------------------------------------------------------------
// GridMap: the mangos-zero server `.map` file the map-extractor bakes per ADT
// grid (height + area + liquid + holes). Parsing it lets the editor overlay the
// height/area/liquid grid the *server* actually uses -- to diff against the
// client ADT (terrain.*). Format verified vs mangos-zero GridMap.h/.cpp.
//
// Two interleaved height grids: V9 = 129x129 outer/corner vertices, V8 =
// 128x128 cell-centre vertices. int8/int16 encodings are decoded to float on
// load (real = gridHeight + packed * (gridMaxHeight - gridHeight) / max).
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>

namespace wf {

constexpr int GRIDMAP_V9 = 129;   // 129x129
constexpr int GRIDMAP_V8 = 128;   // 128x128

struct GridMapArea {
    bool     uniform     = false;        // whole grid is one area id
    uint16_t uniformArea = 0;
    std::array<uint16_t, 16 * 16> grid{}; // 16x16 area ids when !uniform
};

struct GridMapHeight {
    bool  present       = false;
    float gridHeight    = 0.0f;          // flat height when !present or NO_HEIGHT
    float gridMaxHeight = 0.0f;
    std::vector<float> v9;               // 129*129 (empty if flat)
    std::vector<float> v8;               // 128*128
};

struct GridMapLiquid {
    bool     present     = false;
    uint16_t flags       = 0;
    uint16_t liquidType  = 0;            // when typed grid absent
    uint8_t  offsetX = 0, offsetY = 0, width = 0, height = 0;
    float    liquidLevel = 0.0f;
    std::vector<uint16_t> entry;         // 16*16 liquid entry (if typed)
    std::vector<uint8_t>  liquidFlags;   // 16*16 (if typed)
    std::vector<float>    heightMap;     // width*height surface (if has height)
};

struct GridMap {
    uint32_t versionMagic = 0;
    uint32_t buildMagic   = 0;

    bool hasArea = false;   GridMapArea   area;
    bool hasHeightSection = false; GridMapHeight height;
    bool hasLiquid = false; GridMapLiquid liquid;
    bool hasHoles  = false; std::array<uint16_t, 16 * 16> holes{};

    // V9 height at outer-grid (x in 0..128, y in 0..128). Falls back to the
    // flat gridHeight when the section is absent/NO_HEIGHT.
    float heightV9(int x, int y) const;
};

GridMap parseGridMap(const std::vector<uint8_t>& buf);

// ---------------------------------------------------------------------------
// Writer: the exact inverse of parseGridMap, so the editor can emit the
// server-side .map itself after an MCVT edit (write ADT -> write .map ->
// MoveMapGen -> EDITOR_RELOAD_GRID) without the external map-extractor.
// A round-trip through parseGridMap is the acceptance oracle.
// ---------------------------------------------------------------------------

// Writer input is the parser's own in-memory representation.
using GridMapData = GridMap;

struct GridMapWriteOptions {
    float maxErrorU8    = 2.0f;   // worst-case quantization error allowed for uint8 heights
    float maxErrorU16   = 10.0f;  // ... for uint16 heights
    bool  allowQuantize = true;   // false = always absolute float32 heights
};

// Build a GridMapData from raw editor-side grids. v9/v8 empty => flat grid at
// flatHeight (the writer then emits the NO_HEIGHT form). Detects the uniform
// area id case and whether any hole bit is set; gridHeight/gridMaxHeight are
// derived from the height grids. Callers may still set versionMagic/buildMagic
// (or any other field) directly afterwards -- everything is public.
GridMapData makeGridMapData(const std::vector<float>& v9,                // 129*129, or empty = flat
                            const std::vector<float>& v8,                // 128*128, or empty = flat
                            float flatHeight,
                            const std::array<uint16_t, 16 * 16>& areaIds,
                            const std::array<uint16_t, 16 * 16>& holes,  // MCNK 4x4 bitmasks, row-major
                            const GridMapLiquid* liquid = nullptr);

// Serialize to the byte layout parseGridMap accepts (mangos-zero .map).
// Heights pick the smallest encoding whose worst-case quantization error --
// half a step, (max-min)/2^bits/2 -- stays under the option limits; a flat
// grid (absent or all-equal heights) stores no height array at all
// (MAP_HEIGHT_NO_HEIGHT + the single gridHeight). Uniform area ids and
// uniform liquid type/surface collapse to their single-value forms. Section
// offsets/sizes in the header are computed last. Throws on NaN heights and
// on wrongly-sized grids.
std::vector<uint8_t> writeGridMap(const GridMapData& src, const GridMapWriteOptions& opt = {});

} // namespace wf
