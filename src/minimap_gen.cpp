#include "minimap_gen.hpp"

#include <algorithm>
#include <cctype>

#include "coords.hpp"    // TILE_SIZE
#include "crypto.hpp"    // sha1
#include "minimap.hpp"   // MinimapIndex::tileKey (naming mirrored to the reader)
#include "raster.hpp"    // Framebuffer, rasterTexMesh, rasterLiquidMesh, ShadeLight
#include "terrain_render.hpp"  // rasterTerrainSplat

namespace wf {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

// The fixed minimap light: settings colours when elevation shading is on,
// flat white ambient (texel passes through unlit) when it is off.
ShadeLight minimapLight(const MinimapRenderSettings& s) {
    ShadeLight light;
    light.dir = s.lightDir;
    if (s.elevationShading) {
        light.ambient = s.ambient;
        light.diffuse = s.diffuse;
    } else {
        light.ambient = { 1.0f, 1.0f, 1.0f };
        light.diffuse = { 0.0f, 0.0f, 0.0f };
    }
    return light;
}

// Overlay the 16x16 MCNK chunk grid (17 lines each way, borders included) as
// opaque dark pixels -- a debugging/orientation aid, off by default.
void drawChunkGrid(Image& img) {
    const Rgba lineC{ 32, 32, 32, 255 };
    const int  res = img.width;
    for (int i = 0; i <= 16; ++i) {
        int p = std::min(res - 1, i * res / 16);
        for (int q = 0; q < res; ++q) {
            img.at(p, q) = lineC;
            img.at(q, p) = lineC;
        }
    }
}

}  // namespace

Mat4 minimapTileOrtho(int blockX, int blockY) {
    const double T  = TILE_SIZE;
    const double xN = (32.0 - blockY) * T;   // north edge = max world X
    const double xS = xN - T;                // south edge
    const double yW = (32.0 - blockX) * T;   // west edge  = max world Y

    Mat4 m;   // zero-initialised
    // ndc x: worldY = yW (west) -> -1 (left), yW - T (east) -> +1 (right).
    m.at(0, 1) = static_cast<float>(-2.0 / T);
    m.at(0, 3) = static_cast<float>(2.0 * yW / T - 1.0);
    // ndc y: worldX = xN (north) -> +1 (top), xS (south) -> -1 (bottom).
    m.at(1, 0) = static_cast<float>(2.0 / T);
    m.at(1, 3) = static_cast<float>(-2.0 * xS / T - 1.0);
    // ndc z: the rasteriser treats smaller z as nearer, so higher ground must
    // map lower. Linear over +-5000 yd of world Z (beyond any vanilla height).
    m.at(2, 2) = -1.0f / 5000.0f;
    m.at(3, 3) = 1.0f;   // w = 1: orthographic (interpolation degrades to affine)
    return m;
}

Image renderMinimapTile(const TileScene& scene, int blockX, int blockY,
                        const MinimapRenderSettings& settings) {
    const int res = std::max(64, settings.resolution);
    Framebuffer fb(res, res);
    fb.clear(Rgba{ 0, 0, 0, 0 });   // uncovered pixels stay fully transparent

    const Mat4       mvp   = minimapTileOrtho(blockX, blockY);
    const ShadeLight light = minimapLight(settings);

    // Opaque terrain: the MCAL splat path, per chunk, with the baked MCSH
    // shadow map passed through only when the shadow toggle is on.
    const TileRender& tr = scene.terrain;
    for (size_t c = 0; c < tr.chunkMeshes.size(); ++c) {
        const std::vector<uint8_t>* sh = nullptr;
        if (settings.shadows && c < tr.chunkShadows.size() && !tr.chunkShadows[c].empty())
            sh = &tr.chunkShadows[c];
        rasterTerrainSplat(fb, tr.chunkMeshes[c], mvp, tr.chunkLayers[c],
                           tr.tiling, light, sh);
    }

    // Placed doodads / WMO parts -- skipped cleanly when toggled off. The WMO
    // loop mirrors TileScene::renderLit's MOCV/MOLT handling so interiors keep
    // their baked look under the fixed light.
    if (settings.drawM2) {
        for (const TileScene::Inst& in : scene.instances) {
            if (in.mesh >= scene.meshes.size() || in.tex >= scene.textures.size()) continue;
            rasterTexMesh(fb, scene.meshes[in.mesh], mvp * in.transform,
                          *scene.textures[in.tex], light, in.blend);
        }
    }
    if (settings.drawWmo) {
        for (const TileScene::Inst& in : scene.wmoRenderInstances) {
            if (in.mesh >= scene.meshes.size() || in.tex >= scene.textures.size()) continue;
            ShadeLight wl = light;
            if (in.bakedLight) {
                // MOCV: the vertex colour already carries the lighting.
                wl.ambient = { 1, 1, 1 };
                wl.diffuse = { 0, 0, 0 };
            } else {
                wl.ambient = { std::min(1.0f, wl.ambient.x + in.ambientBoost.x),
                               std::min(1.0f, wl.ambient.y + in.ambientBoost.y),
                               std::min(1.0f, wl.ambient.z + in.ambientBoost.z) };
            }
            rasterTexMesh(fb, scene.meshes[in.mesh], mvp * in.transform,
                          *scene.textures[in.tex], wl, in.blend);
        }
    }

    // Translucent liquid last so the blend composites over everything opaque.
    // Water with no terrain beneath still draws (depth-tested, not -written),
    // so water-only pixels are shaded rather than dropped.
    if (settings.drawWater) {
        for (const LiquidSurface& ls : tr.liquids)
            rasterLiquidMesh(fb, ls.mesh, mvp, ls.tint, light, ls.emissive);
    }

    if (settings.adtGridLines) drawChunkGrid(fb.color);
    return std::move(fb.color);
}

std::string minimapHashedName(const std::string& plainName) {
    const std::string key = lower(plainName);
    const auto digest = sha1(reinterpret_cast<const uint8_t*>(key.data()), key.size());
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {   // 16 bytes -> 32 hex chars (md5-length name)
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    out += ".blp";
    return out;
}

TrsEntry makeTrsEntry(const std::string& mapDir, int x, int y) {
    TrsEntry e;
    e.plainName  = MinimapIndex::tileKey(mapDir, x, y);
    e.hashedName = minimapHashedName(e.plainName);
    return e;
}

std::string buildMd5Translate(const std::string& mapDir,
                              const std::vector<TrsEntry>& entries) {
    std::string out = "dir: " + mapDir + "\n";
    for (const TrsEntry& e : entries) {
        if (e.plainName.empty() || e.hashedName.empty()) continue;
        std::string plain = e.plainName;
        if (plain.find('\\') == std::string::npos &&
            plain.find('/') == std::string::npos)
            plain = mapDir + "\\" + plain;   // vanilla writes the full path
        out += plain;
        out += '\t';
        out += e.hashedName;
        out += '\n';
    }
    return out;
}

std::vector<MinimapTileResult> renderMinimapJob(AssetLoader& loader,
                                                const std::string& map,
                                                const MinimapJob& job,
                                                const MinimapRenderSettings& settings) {
    std::vector<MinimapTileResult> out;
    for (const auto& t : job.tiles) {              // set order: deterministic
        TileScene ts = loader.buildTileScene(map, t.first, t.second);
        if (ts.terrain.empty()) continue;          // absent ADT -> no baked tile
        MinimapTileResult r;
        r.x     = t.first;
        r.y     = t.second;
        r.image = renderMinimapTile(ts, t.first, t.second, settings);
        r.trs   = makeTrsEntry(map, t.first, t.second);
        out.push_back(std::move(r));
    }
    return out;
}

Image stitchMinimap(const std::map<std::pair<int, int>, Image>& tiles, int tilePx) {
    if (tiles.empty() || tilePx < 1) return Image();

    int minX = tiles.begin()->first.first, maxX = minX;
    int minY = tiles.begin()->first.second, maxY = minY;
    for (const auto& kv : tiles) {
        minX = std::min(minX, kv.first.first);
        maxX = std::max(maxX, kv.first.first);
        minY = std::min(minY, kv.first.second);
        maxY = std::max(maxY, kv.first.second);
    }

    Image out((maxX - minX + 1) * tilePx, (maxY - minY + 1) * tilePx);
    for (Rgba& p : out.pixels) p = Rgba{ 0, 0, 0, 0 };   // missing = transparent

    for (const auto& kv : tiles) {
        const Image& tile = kv.second;
        if (tile.width <= 0 || tile.height <= 0) continue;
        // Nearest-resample the tile into its tilePx cell (as assembleMinimap).
        const int ox = (kv.first.first - minX) * tilePx;
        const int oy = (kv.first.second - minY) * tilePx;
        for (int ty = 0; ty < tilePx; ++ty) {
            int sy = ty * tile.height / tilePx;
            for (int tx = 0; tx < tilePx; ++tx) {
                int sx = tx * tile.width / tilePx;
                out.at(ox + tx, oy + ty) = tile.at(sx, sy);
            }
        }
    }
    return out;
}

}  // namespace wf
