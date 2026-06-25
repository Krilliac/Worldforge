#include "asset_loader.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "blp.hpp"
#include "coords.hpp"
#include "lighting.hpp"
#include "m2_render.hpp"
#include "wmo_render.hpp"

#include <algorithm>
#include <cmath>

namespace wf {

Vec3 wmoInteriorTint(const std::vector<WmoLight>& lights, float strength) {
    if (lights.empty()) return Vec3{ 0, 0, 0 };
    Vec3 acc{ 0, 0, 0 };
    float wsum = 0.0f;
    for (const WmoLight& L : lights) {
        const float w = std::max(L.intensity, 0.0f);
        acc = acc + L.color * w;
        wsum += w;
    }
    Vec3 avg{ 0, 0, 0 };
    if (wsum > 1e-6f) {
        avg = acc * (1.0f / wsum);                 // intensity-weighted average hue
    } else {                                       // all intensities ~0: plain average
        for (const WmoLight& L : lights) avg = avg + L.color;
        avg = avg * (1.0f / static_cast<float>(lights.size()));
    }
    auto sat = [&](float c) { return std::min(1.0f, std::max(0.0f, c * strength)); };
    return Vec3{ sat(avg.x), sat(avg.y), sat(avg.z) };
}

void TileRender::renderTerrain(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const {
    for (size_t c = 0; c < chunkMeshes.size(); ++c)
        rasterTerrainSplat(fb, chunkMeshes[c], mvp, chunkLayers[c], tiling, lightDir);
}

void TileRender::renderLiquid(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const {
    for (const LiquidSurface& ls : liquids)
        rasterLiquidMesh(fb, ls.mesh, mvp, ls.tint, lightDir, ls.emissive);
}

void TileRender::renderTerrain(Framebuffer& fb, const Mat4& mvp, const ShadeLight& light) const {
    for (size_t c = 0; c < chunkMeshes.size(); ++c)
        rasterTerrainSplat(fb, chunkMeshes[c], mvp, chunkLayers[c], tiling, light);
}

void TileRender::renderLiquid(Framebuffer& fb, const Mat4& mvp, const ShadeLight& light) const {
    for (const LiquidSurface& ls : liquids)
        rasterLiquidMesh(fb, ls.mesh, mvp, ls.tint, light.dir, ls.emissive);
}

Mat4 doodadMatrix(const DoodadDef& d) {
    Vec3 world = placementToWorld(Vec3{ d.pos[0], d.pos[1], d.pos[2] });
    float s = d.scale / 1024.0f;
    Mat4 rot = Mat4::rotateZ(d.rot[1]) * Mat4::rotateY(d.rot[0]) * Mat4::rotateX(d.rot[2]);
    return Mat4::translate(world) * rot * Mat4::scale(Vec3{ s, s, s });
}
Mat4 wmoMatrix(const WmoDef& w) {
    Vec3 world = placementToWorld(Vec3{ w.pos[0], w.pos[1], w.pos[2] });
    Mat4 rot = Mat4::rotateZ(w.rot[1]) * Mat4::rotateY(w.rot[0]) * Mat4::rotateX(w.rot[2]);
    return Mat4::translate(world) * rot;
}

void TileScene::render(Framebuffer& fb, const Mat4& viewProj, Vec3 lightDir) const {
    terrain.renderTerrain(fb, viewProj, lightDir);
    for (const Inst& in : instances)
        rasterTexMesh(fb, meshes[in.mesh], viewProj * in.transform, *textures[in.tex], lightDir);
    for (const Inst& in : wmoRenderInstances)
        rasterTexMesh(fb, meshes[in.mesh], viewProj * in.transform, *textures[in.tex],
                      lightDir, in.blend);
    // Translucent liquid surfaces last (after all opaque geometry) so the
    // alpha-blend composites over the terrain/objects beneath the water.
    terrain.renderLiquid(fb, viewProj, lightDir);
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb, markers, viewProj, opt);
}

void TileScene::renderLit(Framebuffer& fb, const Mat4& viewProj, Vec3 lightDir) const {
    // Same composition as render(), but terrain + liquid use this tile's resolved
    // zone lighting (ShadeLight ambient/diffuse) with the sun direction overridden.
    // Doodads keep the plain directional shade; WMO parts additionally pick up
    // their MOLT interior lift (in.ambientBoost) on top of the zone ambient, so
    // lit interiors aren't black -- a WMO with no MOLT keeps the boost at 0.
    ShadeLight tl = light;        tl.dir = lightDir;
    ShadeLight ll = liquidLight;  ll.dir = lightDir;
    terrain.renderTerrain(fb, viewProj, tl);
    for (const Inst& in : instances)
        rasterTexMesh(fb, meshes[in.mesh], viewProj * in.transform, *textures[in.tex], lightDir);
    for (const Inst& in : wmoRenderInstances) {
        ShadeLight wl = tl;
        wl.ambient = Vec3{ std::min(1.0f, tl.ambient.x + in.ambientBoost.x),
                           std::min(1.0f, tl.ambient.y + in.ambientBoost.y),
                           std::min(1.0f, tl.ambient.z + in.ambientBoost.z) };
        rasterTexMesh(fb, meshes[in.mesh], viewProj * in.transform, *textures[in.tex],
                      wl, in.blend);
    }
    terrain.renderLiquid(fb, viewProj, ll);
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb, markers, viewProj, opt);
}

void AssetLoader::applyLighting(TileScene& ts, const LightDatabase& lights,
                                uint32_t mapId, int x, int y, float dayTick) {
    if (lights.empty()) return;
    // World position at the tile centre. Tile (x,y) covers a TILE_SIZE square whose
    // north/west edges are at (32 - x)/(32 - y) * TILE_SIZE (WoW Z-up world space).
    const float wx = (32.0f - static_cast<float>(x) - 0.5f) * static_cast<float>(TILE_SIZE);
    const float wy = (32.0f - static_cast<float>(y) - 0.5f) * static_cast<float>(TILE_SIZE);
    LightingSample s = lights.lightingAt(Vec3{ wx, wy, 0.0f }, mapId, dayTick);
    if (!s.valid) return;
    ts.light.ambient      = s.ambient;
    ts.light.diffuse      = s.diffuse;
    ts.liquidLight.ambient = s.waterDark;
    ts.liquidLight.diffuse = s.waterLight;
}

std::string AssetLoader::wdtPath(const std::string& map) {
    return "World\\Maps\\" + map + "\\" + map + ".wdt";
}
std::string AssetLoader::adtPath(const std::string& map, int x, int y) {
    return "World\\Maps\\" + map + "\\" + map + "_" +
           std::to_string(x) + "_" + std::to_string(y) + ".adt";
}

std::shared_ptr<const Image> AssetLoader::fallback() {
    if (!fallback_) {
        auto img = std::make_shared<Image>(8, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                img->at(x, y) = ((x ^ y) & 1) ? Rgba{255, 0, 255, 255} : Rgba{40, 40, 40, 255};
        fallback_ = img;
    }
    return fallback_;
}

std::shared_ptr<const Image> AssetLoader::texture(const std::string& path) {
    auto it = texCache_.find(path);
    if (it != texCache_.end()) return it->second;

    std::shared_ptr<const Image> result;
    std::vector<uint8_t> buf;
    if (mpq_.readFile(path, buf)) {
        try {
            result = std::make_shared<Image>(decodeBlp(buf));
        } catch (...) {
            result = fallback();
        }
    } else {
        result = fallback();
    }
    texCache_[path] = result;
    return result;
}

bool AssetLoader::loadWdt(const std::string& map, Wdt& out) {
    std::vector<uint8_t> buf;
    if (!mpq_.readFile(wdtPath(map), buf)) return false;
    out = parseWdt(buf);
    return true;
}

bool AssetLoader::loadAdt(const std::string& map, int x, int y,
                          Adt& adt, std::vector<MapChunk>& chunks) {
    std::vector<uint8_t> buf;
    if (!mpq_.readFile(adtPath(map, x, y), buf)) return false;
    adt = parseAdt(buf);
    chunks = parseChunks(buf);
    return true;
}

namespace {
// MDDF/MMDX reference models by their legacy ".mdx"/".mdl" name, but vanilla MPQs
// store the converted models as ".m2". The client swaps the extension on lookup.
std::string m2Path(const std::string& path) {
    auto endsWith = [&](const char* ext) {
        size_t n = std::char_traits<char>::length(ext);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(path[path.size() - n + i])) != ext[i])
                return false;
        return true;
    };
    if (endsWith(".mdx") || endsWith(".mdl"))
        return path.substr(0, path.size() - 4) + ".m2";
    return path;
}
} // namespace

std::shared_ptr<const M2Model> AssetLoader::model(const std::string& rawPath) {
    const std::string path = m2Path(rawPath);
    auto it = modelCache_.find(path);
    if (it != modelCache_.end()) return it->second;

    std::shared_ptr<const M2Model> result;  // nullptr == missing/malformed
    std::vector<uint8_t> buf;
    if (mpq_.readFile(path, buf)) {
        try {
            result = std::make_shared<M2Model>(parseM2(buf));
        } catch (...) {
            result = nullptr;
        }
    }
    modelCache_[path] = result;
    return result;
}

Aabb AssetLoader::modelBounds(const std::string& path) {
    auto it = boundsCache_.find(path);
    if (it != boundsCache_.end()) return it->second;

    Aabb b;
    if (auto m = model(path)) b = wf::modelBounds(*m);
    boundsCache_[path] = b;
    return b;
}

namespace {
// Root "Path\\Building.wmo" -> group file "Path\\Building_NNN.wmo".
std::string wmoGroupPath(const std::string& root, uint32_t index) {
    std::string base = root;
    const std::string ext = ".wmo";
    if (base.size() >= ext.size() &&
        base.compare(base.size() - ext.size(), ext.size(), ext) == 0)
        base.resize(base.size() - ext.size());
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", index);
    return base + suffix;
}
} // namespace

std::shared_ptr<const WmoModel> AssetLoader::wmo(const std::string& path) {
    auto it = wmoCache_.find(path);
    if (it != wmoCache_.end()) return it->second;

    std::shared_ptr<const WmoModel> result;   // nullptr == missing/malformed root
    std::vector<uint8_t> buf;
    if (mpq_.readFile(path, buf)) {
        try {
            auto m = std::make_shared<WmoModel>();
            m->root = parseWmoRoot(buf);
            for (uint32_t g = 0; g < m->root.nGroups; ++g) {
                std::vector<uint8_t> gbuf;
                if (!mpq_.readFile(wmoGroupPath(path, g), gbuf)) continue;  // skip missing
                try { m->groups.push_back(parseWmoGroup(gbuf)); }
                catch (...) { /* skip a malformed group, keep the rest */ }
            }
            result = m;
        } catch (...) {
            result = nullptr;
        }
    }
    wmoCache_[path] = result;
    return result;
}

bool AssetLoader::sound(const std::string& path, std::vector<uint8_t>& out) const {
    return mpq_.readFile(path, out);
}

AudioClip AssetLoader::soundClip(const std::string& path) const {
    std::vector<uint8_t> buf;
    if (!mpq_.readFile(path, buf)) return AudioClip{};   // empty (codec None)
    return makeClip(path, buf);
}

bool AssetLoader::resolveBigAlpha(const std::string& map, std::optional<bool> override_) {
    if (override_) return *override_;            // explicit caller override
    auto it = bigAlphaCache_.find(map);
    if (it != bigAlphaCache_.end()) return it->second;
    Wdt wdt;
    bool big = loadWdt(map, wdt) ? wdt.bigAlpha() : false;
    bigAlphaCache_.emplace(map, big);
    return big;
}

TileRender AssetLoader::buildTile(const std::string& map, int x, int y,
                                  std::optional<bool> bigAlpha) {
    Adt adt;
    std::vector<MapChunk> chunks;
    if (!loadAdt(map, x, y, adt, chunks)) return TileRender{};   // empty
    return buildTerrain(adt, chunks, x, y, resolveBigAlpha(map, bigAlpha));
}

TileRender AssetLoader::buildTerrain(const Adt& adt, const std::vector<MapChunk>& chunks,
                                     int x, int y, bool bigAlpha) {
    TileRender tile;

    // Decode every MTEX texture once (shared, kept alive by the tile).
    tile.textures.reserve(adt.textures.size());
    for (const std::string& name : adt.textures) tile.textures.push_back(texture(name));

    auto texFor = [&](uint32_t id) -> const Image* {
        return (id < tile.textures.size()) ? tile.textures[id].get() : fallback().get();
    };

    tile.chunkMeshes.reserve(chunks.size());
    tile.chunkAlphas.resize(chunks.size());
    tile.chunkLayers.resize(chunks.size());

    for (size_t c = 0; c < chunks.size(); ++c) {
        const MapChunk& mc = chunks[c];
        tile.chunkMeshes.push_back(buildChunkTexMesh(mc, x, y));

        // Translucent liquid (MCLQ) surface: only chunks that carry water/ocean/
        // magma/slime emit a mesh, and only the wet 8x8 cells become triangles
        // (buildLiquidMesh returns empty otherwise).
        if (mc.hasLiquid) {
            Mesh lm = buildLiquidMesh(mc, x, y);
            if (!lm.indices.empty()) {
                tile.liquids.push_back({ std::move(lm),
                                         liquidTint(mc.liquidType),
                                         liquidEmissive(mc.liquidType),
                                         mc.liquidType });
            }
        }

        std::vector<TerrainLayer>& layers = tile.chunkLayers[c];
        std::vector<AlphaMap>&     alphas = tile.chunkAlphas[c];
        if (mc.layers.empty()) {                          // untextured -> fallback base
            layers.push_back({ fallback().get(), nullptr });
            continue;
        }
        // Layer 0 is the opaque base; layers 1..3 carry an alpha coverage map.
        alphas.reserve(mc.layers.size());
        for (size_t i = 0; i < mc.layers.size(); ++i) {
            if (i == 0) { layers.push_back({ texFor(mc.layers[0].textureId), nullptr }); continue; }
            AlphaMap am = decodeAlphaMap(mc, i, bigAlpha);
            // Draw-time 63->64 edge fix, unless the chunk opts out -- prevents a
            // seam where this layer's coverage meets the next chunk's.
            if (!(mc.flags & MCNK_DO_NOT_FIX_ALPHA)) fixAlphaMapEdges(am);
            alphas.push_back(std::move(am));
        }
        // Second pass: alpha pointers are stable now that `alphas` is filled.
        for (size_t i = 1; i < mc.layers.size(); ++i)
            layers.push_back({ texFor(mc.layers[i].textureId), &alphas[i - 1] });
    }
    return tile;
}

TileScene AssetLoader::buildTileScene(const std::string& map, int x, int y,
                                      std::optional<bool> bigAlpha) {
    TileScene ts;
    Adt adt;
    std::vector<MapChunk> chunks;
    if (!loadAdt(map, x, y, adt, chunks)) return ts;   // empty

    ts.terrain = buildTerrain(adt, chunks, x, y, resolveBigAlpha(map, bigAlpha));

    // Each resolvable M2 doodad becomes a skinned (bind-pose) mesh instance,
    // transformed into world space by its MDDF placement. A missing or malformed
    // model is skipped (a marker still records where it should have been), never
    // fatal -- so a tile renders whatever it can resolve from the archive chain.
    for (const DoodadDef& d : adt.doodads) {
        Vec3 world = placementToWorld(Vec3{ d.pos[0], d.pos[1], d.pos[2] });
        std::shared_ptr<const M2Model> m = model(d.modelName);
        if (!m) {
            ts.markers.cross(world, 2.0f, Rgba{255, 80, 80, 255}, DebugCategory::Marker);
            continue;
        }
        ts.meshes.push_back(skinM2(*m, {}));               // bind pose

        // Pick the model's first usable texture; fall back to the checker.
        std::shared_ptr<const Image> tex;
        for (const std::string& tn : m->textures) {
            if (!tn.empty()) { tex = texture(tn); break; }
        }
        if (!tex) tex = fallback();
        ts.textures.push_back(tex);

        ts.instances.push_back({ ts.meshes.size() - 1, ts.textures.size() - 1,
                                 doodadMatrix(d) });
    }

    // WMO map objects: load the real group geometry (for per-triangle picking /
    // wireframe) placed by the MODF transform; a marker still records the spot.
    for (const WmoDef& w : adt.wmos) {
        Vec3 world = placementToWorld(Vec3{ w.pos[0], w.pos[1], w.pos[2] });
        ts.markers.cross(world, 4.0f, Rgba{120, 180, 255, 255}, DebugCategory::DoodadWire);
        if (auto wm = wmo(w.modelName)) {
            Mat4 xform = wmoMatrix(w);
            Mesh pm = wmoPickMesh(*wm);
            if (!pm.indices.empty())
                ts.wmoInstances.push_back({ std::move(pm), xform, w.uniqueId });
            // Interior lift from this WMO's MOLT lights, shared by all its parts.
            const Vec3 boost = wmoInteriorTint(wm->root.lights);
            // Textured render parts (one per material) -> the shared mesh/texture
            // pools, drawn like doodads.
            for (WmoRenderPart& part : wmoRenderParts(*wm)) {
                if (part.mesh.indices.empty()) continue;
                bool blended = part.blendMode >= 2;        // alpha-blended material
                ts.meshes.push_back(std::move(part.mesh));
                ts.textures.push_back(part.texture.empty() ? fallback() : texture(part.texture));
                ts.wmoRenderInstances.push_back(
                    { ts.meshes.size() - 1, ts.textures.size() - 1, xform, blended, boost });
            }
        }
    }

    return ts;
}

} // namespace wf
