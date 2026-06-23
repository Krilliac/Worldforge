#include "navmesh.hpp"
#include "byte_reader.hpp"

#include <stdexcept>

namespace wf {

namespace {
constexpr uint32_t MMAP_MAGIC        = 0x4d4d4150;  // 'MMAP' as an LE uint32
constexpr uint32_t MMAP_VERSION      = 5;
constexpr int32_t  DT_NAVMESH_MAGIC  = ('D'<<24) | ('N'<<16) | ('A'<<8) | 'V'; // 0x444E4156
constexpr int32_t  DT_NAVMESH_VERSION = 7;
constexpr size_t   MMAP_TILE_HEADER_SIZE = 20;
constexpr size_t   DT_MESH_HEADER_SIZE   = 100;
constexpr size_t   DT_POLY_SIZE          = 32;
} // namespace

NavTile parseMmTile(const std::vector<uint8_t>& buf) {
    NavTile tile;
    ByteReader r(buf);

    // --- MmapTileHeader (20 bytes) ---
    if (r.u32() != MMAP_MAGIC)   throw std::runtime_error("not an .mmtile (bad MMAP magic)");
    uint32_t dtVersion   = r.u32();
    uint32_t mmapVersion = r.u32();
    r.u32();                                  // size of the Detour blob (unused)
    r.u32();                                  // usesLiquids (full 4-byte slot)
    if (mmapVersion != MMAP_VERSION)
        throw std::runtime_error("unsupported MMAP_VERSION");
    if (static_cast<int32_t>(dtVersion) != DT_NAVMESH_VERSION)
        throw std::runtime_error("unsupported Detour navmesh version");

    // --- dtMeshHeader (100 bytes) ---
    const size_t blobStart = MMAP_TILE_HEADER_SIZE;
    ByteReader h(buf.data() + blobStart, buf.size() - blobStart);
    if (h.i32() != DT_NAVMESH_MAGIC) throw std::runtime_error("bad dtMeshHeader magic");
    h.i32();                                  // version (validated above)
    tile.tileX = h.i32();
    tile.tileY = h.i32();
    h.i32();                                  // layer
    h.u32();                                  // userId
    int32_t polyCount = h.i32();
    int32_t vertCount = h.i32();
    h.i32(); h.i32(); h.i32(); h.i32();       // maxLinkCount, detailMesh/Vert/Tri counts
    h.i32(); h.i32(); h.i32();                // bvNodeCount, offMeshConCount, offMeshBase
    h.f32(); h.f32(); h.f32();                // walkableHeight/Radius/Climb
    tile.bmin = { h.f32(), h.f32(), h.f32() };
    tile.bmax = { h.f32(), h.f32(), h.f32() };
    h.f32();                                  // bvQuantFactor
    if (polyCount < 0 || vertCount < 0)
        throw std::runtime_error("negative navmesh counts");

    // --- vertices: float[3] each, right after the header ---
    ByteReader body(buf.data() + blobStart + DT_MESH_HEADER_SIZE,
                    buf.size() - blobStart - DT_MESH_HEADER_SIZE);
    tile.verts.reserve(vertCount);
    for (int32_t i = 0; i < vertCount; ++i)
        tile.verts.push_back({ body.f32(), body.f32(), body.f32() });

    // --- polys: dtPoly (32 bytes) each ---
    tile.polys.reserve(polyCount);
    for (int32_t i = 0; i < polyCount; ++i) {
        NavPoly p;
        body.u32();                           // firstLink
        for (int v = 0; v < 6; ++v) p.verts[v] = body.u16();
        for (int v = 0; v < 6; ++v) body.u16();   // neis
        p.flags = body.u16();
        p.vertCount = body.u8();
        uint8_t areaAndType = body.u8();
        p.area = static_cast<uint8_t>(areaAndType & 0x3F);
        tile.polys.push_back(p);
    }
    return tile;
}

void addNavMesh(DebugDraw& dd, const NavTile& tile, Rgba color, DebugCategory cat) {
    for (const NavPoly& p : tile.polys) {
        const int n = p.vertCount;
        if (n < 2) continue;
        for (int i = 0; i < n; ++i) {
            uint16_t a = p.verts[i];
            uint16_t b = p.verts[(i + 1) % n];        // closes the polygon
            if (a >= tile.verts.size() || b >= tile.verts.size()) continue;
            dd.line(navToWorld(tile.verts[a]), navToWorld(tile.verts[b]), color, cat);
        }
    }
}

} // namespace wf
