#include "asset_loader.hpp"

#include "blp.hpp"

namespace wf {

void TileRender::renderTerrain(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const {
    for (size_t c = 0; c < chunkMeshes.size(); ++c)
        rasterTerrainSplat(fb, chunkMeshes[c], mvp, chunkLayers[c], tiling, lightDir);
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

TileRender AssetLoader::buildTile(const std::string& map, int x, int y, bool bigAlpha) {
    TileRender tile;
    Adt adt;
    std::vector<MapChunk> chunks;
    if (!loadAdt(map, x, y, adt, chunks)) return tile;   // empty

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

} // namespace wf
