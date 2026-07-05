#include "texture_layers.hpp"

#include <algorithm>
#include <cstddef>

namespace wf {

namespace {
// ASCII lower-case fold, enough for MPQ paths (they are ASCII-insensitive;
// no locale involvement wanted here).
char foldChar(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive path equality (MPQ paths are case-insensitive).
bool pathEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (foldChar(a[i]) != foldChar(b[i])) return false;
    return true;
}

// Index of `path` in the MTEX list, case-insensitively, or -1.
int findMtex(const std::vector<std::string>& mtex, const std::string& path) {
    for (size_t i = 0; i < mtex.size(); ++i)
        if (pathEquals(mtex[i], path)) return static_cast<int>(i);
    return -1;
}

// Retarget one chunk's layers fromIdx -> toIdx, then collapse any duplicate
// the swap produced (see swapTexture's header comment). Returns the number of
// layers retargeted. The MCAL blob is only touched when a collapse happens.
int swapInChunk(MapChunk& mc, uint32_t fromIdx, uint32_t toIdx, bool bigAlpha) {
    int retargeted = 0;
    for (TexLayer& layer : mc.layers)
        if (layer.textureId == fromIdx) { layer.textureId = toIdx; ++retargeted; }
    if (retargeted == 0) return 0;

    // Did the swap leave two layers naming the target texture?
    size_t first = mc.layers.size();
    bool   dup   = false;
    for (size_t i = 0; i < mc.layers.size(); ++i) {
        if (mc.layers[i].textureId != toIdx) continue;
        if (first == mc.layers.size()) first = i;
        else { dup = true; break; }
    }
    if (!dup) return retargeted;   // common case: alpha bytes stay untouched

    // Collapse: decode every layer's coverage, max-merge each duplicate into
    // the first occurrence, erase it, and re-encode the blob. decodeAlphaMap
    // yields all-255 for layer 0, so a base-coat `first` absorbs duplicates
    // as a no-op (max(255, x) == 255) and packAlphaLayers skips it anyway.
    std::vector<AlphaMap> maps;
    maps.reserve(mc.layers.size());
    for (size_t i = 0; i < mc.layers.size(); ++i)
        maps.push_back(decodeAlphaMap(mc, i, bigAlpha));
    for (size_t i = mc.layers.size(); i-- > first + 1;) {
        if (mc.layers[i].textureId != toIdx) continue;
        for (size_t k = 0; k < maps[first].texels.size(); ++k)
            maps[first].texels[k] = std::max(maps[first].texels[k], maps[i].texels[k]);
        mc.layers.erase(mc.layers.begin() + static_cast<std::ptrdiff_t>(i));
        maps.erase(maps.begin() + static_cast<std::ptrdiff_t>(i));
    }
    packAlphaLayers(mc, maps, bigAlpha);
    return retargeted;
}
} // namespace

LayerEditResult ensureLayer(MapChunk& mc, std::vector<AlphaMap>& workingAlphas,
                            uint32_t mtexIndex) {
    LayerEditResult res;

    // Existing layer? (Checked before the cap, so a full chunk still answers
    // for textures it already blends.)
    for (size_t i = 0; i < mc.layers.size(); ++i) {
        if (mc.layers[i].textureId == mtexIndex) {
            if (workingAlphas.size() < mc.layers.size())   // empty-blob tolerance
                workingAlphas.resize(mc.layers.size());
            res.layerIndex = static_cast<int>(i);
            return res;
        }
    }

    if (mc.layers.size() >= MCLY_MAX_LAYERS) {
        res.err = LayerEditResult::Err::LayerCapReached;   // nothing mutated
        return res;
    }

    if (workingAlphas.size() < mc.layers.size())           // empty-blob tolerance
        workingAlphas.resize(mc.layers.size());

    TexLayer layer;
    layer.textureId = mtexIndex;
    layer.flags     = mc.layers.empty() ? 0u : MCLY_USE_ALPHA;   // base coat: no alpha
    layer.ofsAlpha  = 0;
    layer.effectId  = MCLY_NO_EFFECT;
    mc.layers.push_back(layer);
    workingAlphas.emplace_back();                           // zeroed = transparent

    res.layerIndex = static_cast<int>(mc.layers.size()) - 1;
    res.created    = true;
    return res;
}

int ensureMtexEntry(std::vector<std::string>& mtex, const std::string& path) {
    int idx = findMtex(mtex, path);
    if (idx >= 0) return idx;
    mtex.push_back(path);
    return static_cast<int>(mtex.size()) - 1;
}

void normalizeLayers(std::vector<AlphaMap>& workingAlphas, size_t editedLayer) {
    if (workingAlphas.size() <= 1) return;   // base coat alone: nothing to scale
    constexpr size_t N = static_cast<size_t>(AlphaMap::DIM) * AlphaMap::DIM;
    for (size_t t = 0; t < N; ++t) {
        int sum = 0;
        for (size_t i = 1; i < workingAlphas.size(); ++i)   // base excluded
            sum += workingAlphas[i].texels[t];
        if (sum <= 255) continue;

        const int edited = (editedLayer >= 1 && editedLayer < workingAlphas.size())
                               ? workingAlphas[editedLayer].texels[t] : 0;
        const int denom = sum - edited;      // > 0 here, since sum > 255 >= edited
        const int scale = 255 - edited;
        for (size_t i = 1; i < workingAlphas.size(); ++i) {
            if (i == editedLayer) continue;  // the fresh stroke is preserved
            int v = (workingAlphas[i].texels[t] * scale + denom / 2) / denom;
            workingAlphas[i].texels[t] = static_cast<uint8_t>(std::min(v, 255));
        }
    }
}

int removeLayer(MapChunk& mc, std::vector<AlphaMap>& workingAlphas,
                size_t layerIndex) {
    if (layerIndex == 0 || layerIndex >= mc.layers.size()) return -1;
    mc.layers.erase(mc.layers.begin() + static_cast<std::ptrdiff_t>(layerIndex));
    if (layerIndex < workingAlphas.size())
        workingAlphas.erase(workingAlphas.begin() + static_cast<std::ptrdiff_t>(layerIndex));
    return static_cast<int>(mc.layers.size());
}

int swapTexture(std::vector<std::string>& mtex, std::vector<MapChunk>& chunks,
                const std::string& fromPath, const std::string& toPath,
                SwapScope scope, int chunkIndex, bool bigAlpha) {
    if (scope == SwapScope::Chunk &&
        (chunkIndex < 0 || static_cast<size_t>(chunkIndex) >= chunks.size()))
        return 0;

    const int fromIdx = findMtex(mtex, fromPath);
    if (fromIdx < 0) return 0;                      // nothing uses fromPath
    const int toIdx = ensureMtexEntry(mtex, toPath);
    if (toIdx == fromIdx) return 0;                 // same entry: no-op

    if (scope == SwapScope::Chunk)
        return swapInChunk(chunks[static_cast<size_t>(chunkIndex)],
                           static_cast<uint32_t>(fromIdx),
                           static_cast<uint32_t>(toIdx), bigAlpha);

    int total = 0;
    for (MapChunk& mc : chunks)
        total += swapInChunk(mc, static_cast<uint32_t>(fromIdx),
                             static_cast<uint32_t>(toIdx), bigAlpha);
    return total;
}

int swapTextureInChunks(std::vector<std::string>& mtex,
                        std::vector<MapChunk>& chunks,
                        const std::string& fromPath, const std::string& toPath,
                        const std::vector<int>& chunkIndices, bool bigAlpha) {
    const int fromIdx = findMtex(mtex, fromPath);
    if (fromIdx < 0) return 0;
    const int toIdx = ensureMtexEntry(mtex, toPath);
    if (toIdx == fromIdx) return 0;

    int total = 0;
    for (int ci : chunkIndices) {
        if (ci < 0 || static_cast<size_t>(ci) >= chunks.size()) continue;
        total += swapInChunk(chunks[static_cast<size_t>(ci)],
                             static_cast<uint32_t>(fromIdx),
                             static_cast<uint32_t>(toIdx), bigAlpha);
    }
    return total;
}

void commitAlphas(MapChunk& mc, const std::vector<AlphaMap>& workingAlphas,
                  bool bigAlpha) {
    // packAlphaLayers already rebuilds the MCAL blob, refreshes each MCLY
    // entry's ofsAlpha and use-alpha flag, and strips the compression flag
    // (we never emit RLE) -- delegation is the whole job.
    packAlphaLayers(mc, workingAlphas, bigAlpha);
}

} // namespace wf
