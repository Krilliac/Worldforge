#pragma once
// ---------------------------------------------------------------------------
// AssetLoader: the last mile -- read real client files from an MPQ chain and
// turn them into renderable data. Decodes BLP textures (cached), parses the WDT
// (which tiles exist) and ADT tiles (terrain + placements), and assembles a
// tile's textured terrain (MCNK/MCAL splat using the MTEX textures). The same
// MpqManager wforge-dump already uses; point it at a real Data dir.
//
// Tested headlessly by writing synthetic ADT/BLP files into an MPQ with
// writeMpqArchive, then loading them back -- no copyrighted assets needed.
// ---------------------------------------------------------------------------
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mpq.hpp"
#include "wow_files.hpp"
#include "terrain.hpp"
#include "terrain_render.hpp"
#include "raster.hpp"
#include "image.hpp"

namespace wf {

// Owns the meshes / textures / alpha maps for one rendered tile, so the
// TerrainLayer pointers stay valid while the tile is drawn.
struct TileRender {
    std::vector<TexMesh>                      chunkMeshes;   // per MCNK
    std::vector<std::vector<AlphaMap>>        chunkAlphas;   // [chunk][overlay layer]
    std::vector<std::vector<TerrainLayer>>    chunkLayers;   // [chunk] -> splat layers
    std::vector<std::shared_ptr<const Image>> textures;      // MTEX, keep-alive
    float tiling = 8.0f;

    bool empty() const { return chunkMeshes.empty(); }
    // Rasterise every chunk's terrain splat into `fb` through `mvp`.
    void renderTerrain(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const;
};

class AssetLoader {
public:
    explicit AssetLoader(const MpqManager& mpq) : mpq_(mpq) {}

    // Decode a BLP by archived path (cached). Returns a magenta/checker fallback
    // if the file is missing or undecodable, so rendering never fails hard.
    std::shared_ptr<const Image> texture(const std::string& path);

    // Parse a map's WDT. Returns false (and leaves `out` default) if absent.
    bool loadWdt(const std::string& map, Wdt& out);

    // Parse one ADT tile: placements (Adt) + terrain chunks. False if absent.
    bool loadAdt(const std::string& map, int x, int y, Adt& adt, std::vector<MapChunk>& chunks);

    // Build a tile's textured terrain (MCNK meshes + MCAL splat layers from
    // MTEX). bigAlpha selects the 8-bit vs 4-bit MCAL form (WDT MPHD flag).
    TileRender buildTile(const std::string& map, int x, int y, bool bigAlpha = false);

private:
    static std::string wdtPath(const std::string& map);
    static std::string adtPath(const std::string& map, int x, int y);
    std::shared_ptr<const Image> fallback();

    const MpqManager& mpq_;
    std::unordered_map<std::string, std::shared_ptr<const Image>> texCache_;
    std::shared_ptr<const Image> fallback_;
};

} // namespace wf
