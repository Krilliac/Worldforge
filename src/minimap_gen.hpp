#pragma once
// ---------------------------------------------------------------------------
// Headless minimap generator: render client-style per-ADT-tile minimap images
// fully offline with the software rasteriser (no GPU), plus the md5translate
// indirection WRITER (the counterpart of minimap.hpp's reader) and a stitcher
// that assembles per-tile renders into one whole-map overview image.
//
// Why: the live-edit loop needs the client to see a correct minimap for edited
// terrain, and raster.cpp already IS an offline renderer -- so a top-down
// orthographic pass over a loaded TileScene (the same data the viewport draws)
// produces a per-tile image with the exact 533.33333-yd ADT footprint the
// client's baked tiles cover.
//
// Output is deliberately uniform: a FIXED global light (a plain
// Eastern-Kingdoms-noon stand-in, NOT the viewport/zone lighting) so every
// tile of a map shades identically no matter when or where it was rendered,
// and nothing here reads the clock or RNG -- two renders of the same scene are
// byte-identical (golden-image friendly).
//
// KNOWN GAP -- .blp encoding: the client consumes minimap tiles as BLP files
// under textures\Minimap\<hash>.blp, but blp.hpp is decode-only (no encoder
// yet). Until a BLP writer exists, emit the rendered tiles as .png via
// image.hpp writePng; the .trs table below already names the final .blp files
// so only the encode step is missing.
//
// Shadow note: MCSH sampling reuses the shared splat path
// (rasterTerrainSplat), so shadowed texels darken by the engine-wide MCSH
// factor defined in terrain_render (the client's 178/256) -- one shadow look
// everywhere, minimap included.
// ---------------------------------------------------------------------------
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "asset_loader.hpp"   // TileScene, AssetLoader
#include "image.hpp"          // Image
#include "math.hpp"           // Vec3, Mat4

namespace wf {

// How a minimap tile is rendered. `resolution` is the square output size in
// pixels -- any value >= 64 is allowed (not just powers of two; values below
// 64 are clamped up). The light terms are a fixed "default Eastern Kingdoms
// global light" so output is uniform regardless of viewport/zone lighting;
// they are only applied when elevationShading is on (off = flat/unlit texels).
struct MinimapRenderSettings {
    int  resolution       = 512;
    bool drawM2           = true;    // draw placed doodad instances
    bool drawWmo          = true;    // draw placed WMO render parts
    bool drawWater        = true;    // composite the translucent MCLQ surfaces
    bool adtGridLines     = false;   // overlay the 16x16 MCNK chunk grid
    bool elevationShading = true;    // simple N.L with the fixed light below
    bool shadows          = true;    // sample the baked MCSH shadow maps
    // Fixed global light (sun high in the sky, slightly warm diffuse over a
    // slightly cool ambient). Deliberately constant defaults, not Light.dbc.
    Vec3 lightDir{ 0.35f, 0.25f, 0.90f };
    Vec3 diffuse { 0.62f, 0.62f, 0.58f };
    Vec3 ambient { 0.50f, 0.50f, 0.55f };
};

// Orthographic top-down projection (camera looking straight down -Z in WoW
// world space) framing exactly the TILE_SIZE x TILE_SIZE extent of ADT tile
// (blockX, blockY): north edge -> top row, west edge -> left column -- the
// orientation of the client's baked map<x>_<y> tiles. Higher ground is nearer
// (wins the depth test); world Z is mapped linearly with +-5000 yd headroom.
Mat4 minimapTileOrtho(int blockX, int blockY);

// Render one loaded tile to a minimap image. `scene` is the streamed tile as
// AssetLoader::buildTileScene exposes it (terrain splat layers + MCSH shadows
// + liquid surfaces + placed M2/WMO instances); blockX/blockY are its ADT
// filename indices (they frame the camera -- a synthetic TileScene needs no
// hasSource). Terrain draws through the MCAL splat path, water as the
// translucent MCLQ pass, and instance categories are skipped cleanly when
// toggled off. Pixels nothing covers stay fully transparent (0,0,0,0), so a
// tile with zero placements is just its terrain and stitching keeps holes
// see-through.
Image renderMinimapTile(const TileScene& scene, int blockX, int blockY,
                        const MinimapRenderSettings& settings = {});

// One md5translate.trs table row: the logical tile name and the stored
// (hashed) file name it redirects to, e.g.
//   plainName  "azeroth\map32_48.blp"
//   hashedName "9d7fe989cb572c88aff61fb79864f26f.blp".
struct TrsEntry {
    std::string plainName;
    std::string hashedName;
};

// Stored file name for a plain tile name: 32 lowercase hex chars + ".blp".
// The client only needs the name to be a unique, stable indirection, so this
// is the truncated SHA-1 of the lowercased plain name (crypto.hpp has no MD5;
// SHA-1 is the existing tested digest here -- an md5-LENGTH stand-in, not
// MD5). Same input always yields the same name; distinct tiles collide with
// negligible probability.
std::string minimapHashedName(const std::string& plainName);

// Build the table row for map tile (x, y): plainName via
// MinimapIndex::tileKey (the exact key the reader resolves) + its hashed name.
TrsEntry makeTrsEntry(const std::string& mapDir, int x, int y);

// Serialise entries to md5translate.trs text for one map section, in the
// exact syntax MinimapIndex::parse (minimap.cpp) reads back:
//   dir: <MapName>
//   <MapName>\map<X>_<Y>.blp<TAB><hash>.blp
// A bare plainName (no path separator) is prefixed with mapDir so the left
// column is always the full logical path, matching the vanilla file. Entries
// with an empty name are skipped. Round-tripping through MinimapIndex::parse
// is the unit-test oracle.
std::string buildMd5Translate(const std::string& mapDir,
                              const std::vector<TrsEntry>& entries);

// A batch of tiles to render. The caller decides the selection -- the current
// tile, an explicit set, or every tile flagged in the WDT MAIN table -- and
// passes the coordinates; this module stays IO-light and never scans archives
// itself. Tile key: (x = column, y = row), the ADT filename order.
struct MinimapJob {
    std::set<std::pair<int, int>> tiles;
};

// One rendered tile of a job: its coordinates, the image, and its .trs row.
struct MinimapTileResult {
    int      x = 0, y = 0;
    Image    image;
    TrsEntry trs;
};

// Run a job: load each requested tile through `loader` (buildTileScene),
// render it, and pair it with its TrsEntry. Tiles with no terrain (absent
// ADT) produce no result -- exactly like the client, which has no baked tile
// there. Results come back in the job's set order (ascending x, then y), so
// the run is deterministic.
std::vector<MinimapTileResult> renderMinimapJob(AssetLoader& loader,
                                                const std::string& map,
                                                const MinimapJob& job,
                                                const MinimapRenderSettings& settings = {});

// Assemble per-tile images into one whole-map picture at `tilePx` pixels per
// tile (nearest-resampled, like minimap.cpp's assembleMinimap). The output
// covers the bounding box of the given tile coordinates; tile (x, y) lands at
// pixel offset ((x - minX) * tilePx, (y - minY) * tilePx) and grid cells with
// no tile stay fully transparent. An empty input yields an empty image.
// Write the result out with image.hpp writePng.
Image stitchMinimap(const std::map<std::pair<int, int>, Image>& tiles,
                    int tilePx = 256);

}  // namespace wf
