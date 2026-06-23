#include "asset_loader.hpp"

#include "blp.hpp"
#include "coords.hpp"
#include "m2_render.hpp"

namespace wf {

void TileRender::renderTerrain(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const {
    for (size_t c = 0; c < chunkMeshes.size(); ++c)
        rasterTerrainSplat(fb, chunkMeshes[c], mvp, chunkLayers[c], tiling, lightDir);
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
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb, markers, viewProj, opt);
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

std::shared_ptr<const M2Model> AssetLoader::model(const std::string& path) {
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

TileRender AssetLoader::buildTile(const std::string& map, int x, int y, bool bigAlpha) {
    Adt adt;
    std::vector<MapChunk> chunks;
    if (!loadAdt(map, x, y, adt, chunks)) return TileRender{};   // empty
    return buildTerrain(adt, chunks, x, y, bigAlpha);
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
            alphas.push_back(decodeAlphaMap(mc, i, bigAlpha));
        }
        // Second pass: alpha pointers are stable now that `alphas` is filled.
        for (size_t i = 1; i < mc.layers.size(); ++i)
            layers.push_back({ texFor(mc.layers[i].textureId), &alphas[i - 1] });
    }
    return tile;
}

TileScene AssetLoader::buildTileScene(const std::string& map, int x, int y, bool bigAlpha) {
    TileScene ts;
    Adt adt;
    std::vector<MapChunk> chunks;
    if (!loadAdt(map, x, y, adt, chunks)) return ts;   // empty

    ts.terrain = buildTerrain(adt, chunks, x, y, bigAlpha);

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

    // WMO map objects get a placement marker for now (full multi-file WMO
    // geometry is a follow-up); their world position proves the MODF transform.
    for (const WmoDef& w : adt.wmos) {
        Vec3 world = placementToWorld(Vec3{ w.pos[0], w.pos[1], w.pos[2] });
        ts.markers.cross(world, 4.0f, Rgba{120, 180, 255, 255}, DebugCategory::DoodadWire);
    }

    return ts;
}

} // namespace wf
