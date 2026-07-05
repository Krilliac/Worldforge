#include "gridmap.hpp"
#include "byte_reader.hpp"
#include "byte_writer.hpp"

#include <cmath>
#include <stdexcept>

namespace wf {

namespace {
constexpr uint16_t MAP_AREA_NO_AREA    = 0x0001;
constexpr uint32_t MAP_HEIGHT_NO_HEIGHT = 0x0001;
constexpr uint32_t MAP_HEIGHT_AS_INT16  = 0x0002;
constexpr uint32_t MAP_HEIGHT_AS_INT8   = 0x0004;
constexpr uint16_t MAP_LIQUID_NO_TYPE   = 0x0001;
constexpr uint16_t MAP_LIQUID_NO_HEIGHT = 0x0002;

constexpr size_t GRIDMAP_HEADER_SIZE = 44;   // "MAPS" + 2 magics + 4 (ofs,size) pairs

void wraw(ByteWriter& w, const char* fourcc) {
    w.bytes(reinterpret_cast<const uint8_t*>(fourcc), 4);
}

template <typename T>
bool allEqual(const T* p, size_t n) {
    for (size_t i = 1; i < n; ++i)
        if (p[i] != p[0]) return false;
    return true;
}

// Encode v as round((v - base) * scale), clamped to [0, maxPacked]. The
// parser decodes base + packed / scale, so worst-case error is half a step.
uint32_t quantize(float v, float base, float scale, uint32_t maxPacked) {
    const float q = (v - base) * scale + 0.5f;
    if (!(q > 0.0f)) return 0;
    if (q >= static_cast<float>(maxPacked)) return maxPacked;
    return static_cast<uint32_t>(q);
}
} // namespace

float GridMap::heightV9(int x, int y) const {
    if (!hasHeightSection || !height.present || height.v9.empty())
        return height.gridHeight;
    if (x < 0 || y < 0 || x >= GRIDMAP_V9 || y >= GRIDMAP_V9)
        return height.gridHeight;
    return height.v9[static_cast<size_t>(y) * GRIDMAP_V9 + x];
}

GridMap parseGridMap(const std::vector<uint8_t>& buf) {
    GridMap m;
    ByteReader r(buf);

    if (r.fourccRaw() != "MAPS")
        throw std::runtime_error("not a GridMap (.map) file");
    m.versionMagic = r.u32();
    m.buildMagic   = r.u32();
    const uint32_t areaOfs   = r.u32(); r.u32();   // areaMapSize (unused)
    const uint32_t heightOfs = r.u32(); r.u32();
    const uint32_t liquidOfs = r.u32(); r.u32();
    const uint32_t holesOfs  = r.u32(); r.u32();

    // --- area ---
    if (areaOfs) {
        ByteReader a(buf.data() + areaOfs, buf.size() - areaOfs);
        if (a.fourccRaw() != "AREA") throw std::runtime_error("bad AREA magic");
        uint16_t flags    = a.u16();
        uint16_t gridArea = a.u16();
        m.hasArea = true;
        if (flags & MAP_AREA_NO_AREA) {
            m.area.uniform = true;
            m.area.uniformArea = gridArea;
        } else {
            for (auto& v : m.area.grid) v = a.u16();   // 16*16 row-major
        }
    }

    // --- height ---
    if (heightOfs) {
        ByteReader h(buf.data() + heightOfs, buf.size() - heightOfs);
        if (h.fourccRaw() != "MHGT") throw std::runtime_error("bad MHGT magic");
        uint32_t flags = h.u32();
        m.height.gridHeight    = h.f32();
        m.height.gridMaxHeight = h.f32();
        m.hasHeightSection = true;

        if (flags & MAP_HEIGHT_NO_HEIGHT) {
            m.height.present = false;                  // flat at gridHeight
        } else {
            m.height.present = true;
            m.height.v9.resize(GRIDMAP_V9 * GRIDMAP_V9);
            m.height.v8.resize(GRIDMAP_V8 * GRIDMAP_V8);
            if (flags & MAP_HEIGHT_AS_INT16) {
                float mult = (m.height.gridMaxHeight - m.height.gridHeight) / 65535.0f;
                for (float& v : m.height.v9) v = m.height.gridHeight + h.u16() * mult;
                for (float& v : m.height.v8) v = m.height.gridHeight + h.u16() * mult;
            } else if (flags & MAP_HEIGHT_AS_INT8) {
                float mult = (m.height.gridMaxHeight - m.height.gridHeight) / 255.0f;
                for (float& v : m.height.v9) v = m.height.gridHeight + h.u8() * mult;
                for (float& v : m.height.v8) v = m.height.gridHeight + h.u8() * mult;
            } else {
                for (float& v : m.height.v9) v = h.f32();   // absolute floats
                for (float& v : m.height.v8) v = h.f32();
            }
        }
    }

    // --- liquid ---
    if (liquidOfs) {
        ByteReader l(buf.data() + liquidOfs, buf.size() - liquidOfs);
        if (l.fourccRaw() != "MLIQ") throw std::runtime_error("bad MLIQ magic");
        GridMapLiquid& lq = m.liquid;
        lq.flags      = l.u16();
        lq.liquidType = l.u16();
        lq.offsetX    = l.u8();
        lq.offsetY    = l.u8();
        lq.width      = l.u8();
        lq.height     = l.u8();
        lq.liquidLevel = l.f32();
        m.hasLiquid = true;

        if (!(lq.flags & MAP_LIQUID_NO_TYPE)) {
            lq.entry.resize(16 * 16);
            lq.liquidFlags.resize(16 * 16);
            for (auto& v : lq.entry)       v = l.u16();
            for (auto& v : lq.liquidFlags) v = l.u8();
        }
        if (!(lq.flags & MAP_LIQUID_NO_HEIGHT)) {
            lq.present = true;
            const size_t n = static_cast<size_t>(lq.width) * lq.height;
            lq.heightMap.resize(n);
            for (float& v : lq.heightMap) v = l.f32();
        }
    }

    // --- holes ---
    if (holesOfs) {
        ByteReader ho(buf.data() + holesOfs, buf.size() - holesOfs);
        if (ho.remaining() >= 16u * 16u * 2u) {
            for (auto& v : m.holes) v = ho.u16();      // 16x16 uint16 bitmasks
            m.hasHoles = true;
        }
    }

    return m;
}

GridMapData makeGridMapData(const std::vector<float>& v9,
                            const std::vector<float>& v8,
                            float flatHeight,
                            const std::array<uint16_t, 16 * 16>& areaIds,
                            const std::array<uint16_t, 16 * 16>& holes,
                            const GridMapLiquid* liquid) {
    GridMapData m;

    m.hasArea = true;
    m.area.grid = areaIds;
    if (allEqual(areaIds.data(), areaIds.size())) {
        m.area.uniform = true;
        m.area.uniformArea = areaIds[0];
    }

    m.hasHeightSection = true;
    if (v9.empty() && v8.empty()) {
        m.height.present = false;                  // flat at flatHeight
        m.height.gridHeight = m.height.gridMaxHeight = flatHeight;
    } else {
        if (v9.size() != size_t(GRIDMAP_V9) * GRIDMAP_V9 ||
            v8.size() != size_t(GRIDMAP_V8) * GRIDMAP_V8)
            throw std::runtime_error("makeGridMapData: V9/V8 grid size mismatch");
        m.height.present = true;
        m.height.v9 = v9;
        m.height.v8 = v8;
        float mn = v9[0], mx = v9[0];
        for (float v : v9) { if (v < mn) mn = v; if (v > mx) mx = v; }
        for (float v : v8) { if (v < mn) mn = v; if (v > mx) mx = v; }
        m.height.gridHeight    = mn;
        m.height.gridMaxHeight = mx;
    }

    if (liquid) {
        m.hasLiquid = true;
        m.liquid = *liquid;
    }

    m.holes = holes;
    for (uint16_t h : holes)
        if (h) { m.hasHoles = true; break; }

    return m;
}

std::vector<uint8_t> writeGridMap(const GridMapData& src, const GridMapWriteOptions& opt) {
    // --- area section ---
    ByteWriter area;
    if (src.hasArea) {
        wraw(area, "AREA");
        const bool uniform = src.area.uniform ||
                             allEqual(src.area.grid.data(), src.area.grid.size());
        if (uniform) {
            area.u16(MAP_AREA_NO_AREA);
            area.u16(src.area.uniform ? src.area.uniformArea : src.area.grid[0]);
        } else {
            area.u16(0);
            area.u16(0);                           // gridArea unused in gridded form
            for (uint16_t v : src.area.grid) area.u16(v);
        }
    }

    // --- height section ---
    ByteWriter height;
    if (src.hasHeightSection) {
        const GridMapHeight& hs = src.height;
        float minH = hs.gridHeight, maxH = hs.gridHeight;
        if (hs.present) {
            if (hs.v9.size() != size_t(GRIDMAP_V9) * GRIDMAP_V9 ||
                hs.v8.size() != size_t(GRIDMAP_V8) * GRIDMAP_V8)
                throw std::runtime_error("writeGridMap: V9/V8 grid size mismatch");
            minH = maxH = hs.v9[0];
            auto scan = [&](const std::vector<float>& g) {
                for (float v : g) {
                    if (std::isnan(v)) throw std::runtime_error("writeGridMap: NaN height");
                    if (v < minH) minH = v;
                    if (v > maxH) maxH = v;
                }
            };
            scan(hs.v9);
            scan(hs.v8);
        }
        const float diff = maxH - minH;
        // A completely flat grid stores no height array at all -- this also
        // guards the quantization multiplier against div-by-zero (diff == 0).
        const bool flat = !hs.present || diff <= 0.0f;

        uint32_t flags = 0;
        if (flat)
            flags = MAP_HEIGHT_NO_HEIGHT;
        else if (opt.allowQuantize) {
            // Smallest encoding whose worst-case error (half a quantization
            // step) stays under the configured limit.
            if (diff / 255.0f * 0.5f <= opt.maxErrorU8)
                flags = MAP_HEIGHT_AS_INT8;
            else if (diff / 65535.0f * 0.5f <= opt.maxErrorU16)
                flags = MAP_HEIGHT_AS_INT16;
        }

        wraw(height, "MHGT");
        height.u32(flags);
        height.f32(minH);                          // gridHeight
        height.f32(maxH);                          // gridMaxHeight
        if (!flat) {
            if (flags & MAP_HEIGHT_AS_INT8) {
                const float scale = 255.0f / diff;
                for (float v : hs.v9) height.u8(static_cast<uint8_t>(quantize(v, minH, scale, 255)));
                for (float v : hs.v8) height.u8(static_cast<uint8_t>(quantize(v, minH, scale, 255)));
            } else if (flags & MAP_HEIGHT_AS_INT16) {
                const float scale = 65535.0f / diff;
                for (float v : hs.v9) height.u16(static_cast<uint16_t>(quantize(v, minH, scale, 65535)));
                for (float v : hs.v8) height.u16(static_cast<uint16_t>(quantize(v, minH, scale, 65535)));
            } else {
                for (float v : hs.v9) height.f32(v);   // absolute floats, bit-exact
                for (float v : hs.v8) height.f32(v);
            }
        }
    }

    // --- liquid section ---
    ByteWriter liquid;
    if (src.hasLiquid) {
        const GridMapLiquid& lq = src.liquid;
        const bool haveTypeGrids = lq.entry.size() == 16 * 16 &&
                                   lq.liquidFlags.size() == 16 * 16;
        // Uniform type collapses to the single header liquidType (the format
        // has nowhere to keep a per-cell flags value in that form).
        const bool typed = haveTypeGrids &&
            !(allEqual(lq.entry.data(), lq.entry.size()) &&
              allEqual(lq.liquidFlags.data(), lq.liquidFlags.size()));
        const bool haveSurface = lq.present && !lq.heightMap.empty() &&
            lq.heightMap.size() == static_cast<size_t>(lq.width) * lq.height;
        // A uniform surface collapses to NO_HEIGHT + the single liquidLevel.
        const bool surfaced = haveSurface &&
            !allEqual(lq.heightMap.data(), lq.heightMap.size());

        uint16_t flags = lq.flags & ~(MAP_LIQUID_NO_TYPE | MAP_LIQUID_NO_HEIGHT);
        uint16_t type  = lq.liquidType;
        if (!typed) {
            flags |= MAP_LIQUID_NO_TYPE;
            if (haveTypeGrids) type = lq.entry[0];      // collapsed uniform type
        }
        float level = lq.liquidLevel;
        if (!surfaced) {
            flags |= MAP_LIQUID_NO_HEIGHT;
            if (haveSurface) level = lq.heightMap[0];   // collapsed uniform surface
        }

        wraw(liquid, "MLIQ");
        liquid.u16(flags);
        liquid.u16(type);
        liquid.u8(lq.offsetX);
        liquid.u8(lq.offsetY);
        liquid.u8(lq.width);
        liquid.u8(lq.height);
        liquid.f32(level);
        if (typed) {
            for (uint16_t v : lq.entry)      liquid.u16(v);
            for (uint8_t  v : lq.liquidFlags) liquid.u8(v);
        }
        if (surfaced) {
            for (float v : lq.heightMap) {
                if (std::isnan(v)) throw std::runtime_error("writeGridMap: NaN liquid height");
                liquid.f32(v);
            }
        }
    }

    // --- holes section (no magic, raw 16x16 uint16 -- mirrors the parser) ---
    ByteWriter holes;
    if (src.hasHoles)
        for (uint16_t v : src.holes) holes.u16(v);

    // --- header: offsets/sizes computed last, absent sections get (0, 0) ---
    ByteWriter out;
    wraw(out, "MAPS");
    out.u32(src.versionMagic);
    out.u32(src.buildMagic);
    uint32_t ofs = GRIDMAP_HEADER_SIZE;
    auto section = [&](const ByteWriter& w) {
        if (w.size() == 0) { out.u32(0); out.u32(0); return; }
        out.u32(ofs);
        out.u32(static_cast<uint32_t>(w.size()));
        ofs += static_cast<uint32_t>(w.size());
    };
    section(area);
    section(height);
    section(liquid);
    section(holes);

    std::vector<uint8_t> buf = out.take();
    auto append = [&](const ByteWriter& w) {
        buf.insert(buf.end(), w.data().begin(), w.data().end());
    };
    append(area);
    append(height);
    append(liquid);
    append(holes);
    return buf;
}

} // namespace wf
