#include "gridmap.hpp"
#include "byte_reader.hpp"

#include <stdexcept>

namespace wf {

namespace {
constexpr uint16_t MAP_AREA_NO_AREA    = 0x0001;
constexpr uint32_t MAP_HEIGHT_NO_HEIGHT = 0x0001;
constexpr uint32_t MAP_HEIGHT_AS_INT16  = 0x0002;
constexpr uint32_t MAP_HEIGHT_AS_INT8   = 0x0004;
constexpr uint16_t MAP_LIQUID_NO_TYPE   = 0x0001;
constexpr uint16_t MAP_LIQUID_NO_HEIGHT = 0x0002;
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

} // namespace wf
