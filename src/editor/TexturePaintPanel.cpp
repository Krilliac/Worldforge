#include "editor/TexturePaintPanel.hpp"

#include <algorithm>

#include "coords.hpp"   // chunkCornerWorld, CHUNK_SIZE
#include "editing.hpp"  // paintAlpha, Falloff
#include "imgui.h"

namespace wf::editor {

// Mirrors TerrainToolPanel's profile order.
static const Falloff kFalloffs[] = {
    Falloff::Flat, Falloff::Linear, Falloff::Smooth, Falloff::Gaussian,
};
static const char* kFalloffNames[] = { "Flat", "Linear", "Smooth", "Gaussian" };
static constexpr int kFalloffCount = 4;

int TexturePaintPanel::paint(std::vector<std::vector<AlphaMap>>& chunkAlphas,
                             const std::vector<MapChunk>& chunks,
                             int blockX, int blockY, Vec3 center) const {
    if (layer_ < 1) return 0;
    const size_t slot   = static_cast<size_t>(layer_ - 1);   // index into chunkAlphas[c]
    const uint8_t target = erase_ ? 0u : 255u;
    const float CH      = static_cast<float>(CHUNK_SIZE);
    const float radUV   = radius_ / CH;
    const int   fi      = (falloff_ >= 0 && falloff_ < kFalloffCount) ? falloff_ : 0;
    const Falloff fo    = kFalloffs[fi];

    int hits = 0;
    const size_t n = std::min(chunks.size(), chunkAlphas.size());
    for (size_t c = 0; c < n; ++c) {
        if (slot >= chunkAlphas[c].size()) continue;          // chunk lacks this slot
        const MapChunk& mc = chunks[c];
        const Vec3 corner = chunkCornerWorld(blockX, blockY,
                                             static_cast<int>(mc.indexY),
                                             static_cast<int>(mc.indexX), 0.0f);
        // Chunk-local UV: u = west->east (col), v = north->south (row); matches
        // buildChunkTexMesh's vertex UVs and splatSample's (u->col, v->row).
        const float u = (corner.y - center.y) / CH;
        const float v = (corner.x - center.x) / CH;
        // Cheap reject: brush can't reach a chunk whose UV box is beyond radius.
        if (u < -radUV || u > 1.0f + radUV || v < -radUV || v > 1.0f + radUV) continue;
        hits += paintAlpha(chunkAlphas[c][slot], u, v, radUV, strength_, fo, target);
    }
    return hits;
}

void TexturePaintPanel::draw() {
    ImGui::Begin("Texture Paint");

    ImGui::Checkbox("Paint on drag", &enabled_);
    ImGui::SliderInt("Layer", &layer_, 1, 3);
    ImGui::Checkbox("Erase", &erase_);
    ImGui::SliderFloat("Radius", &radius_, 1.0f, 100.0f);
    ImGui::SliderFloat("Strength", &strength_, 0.0f, 1.0f);
    ImGui::Combo("Falloff", &falloff_, kFalloffNames, kFalloffCount);

    ImGui::End();
}

} // namespace wf::editor
