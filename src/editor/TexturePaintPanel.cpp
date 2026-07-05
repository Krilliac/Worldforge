#include "editor/TexturePaintPanel.hpp"

#include <algorithm>

#include "coords.hpp"   // chunkCornerWorld, CHUNK_SIZE
#include "editing.hpp"  // paintAlpha, sprayAlpha, Falloff
#include "imgui.h"

namespace wf::editor {

// Mirrors TerrainToolPanel's profile order (append-only; indices serialise).
static const Falloff kFalloffs[] = {
    Falloff::Flat,       Falloff::Linear,        Falloff::Smooth,
    Falloff::Gaussian,   Falloff::Polynomial,    Falloff::Trigonometric,
    Falloff::Quadratic,
};
static const char* kFalloffNames[] = {
    "Flat", "Linear", "Smooth", "Gaussian", "Polynomial", "Trigonometric",
    "Quadratic",
};
static constexpr int kFalloffCount = 7;

void TexturePaintPanel::setOpacityHotkey(int slot) {
    static const int kLevels[5] = { 255, 191, 127, 63, 0 };
    if (slot >= 1 && slot <= 5) targetAlpha_ = kLevels[slot - 1];
}

void TexturePaintPanel::beginStrokeIfNeeded(
        std::vector<std::vector<AlphaMap>>& chunkAlphas) {
    if (!stack_ || strokeOpen_) return;
    stack_->begin(AF_Alpha, erase_ ? "Erase texture" : "Paint texture",
                  MergeMode::Disable);
    strokeAlphas_ = &chunkAlphas;
    strokeOpen_   = true;
}

// Snapshot ALL of a chunk's blend-layer slots (the working 64x64 buffers) --
// AF_Alpha gates the capture, so heights/shadow/etc are never stored (the
// stack's memory-bounding contract).
CaptureFns TexturePaintPanel::alphaCapture() const {
    CaptureFns fns;
    const std::vector<std::vector<AlphaMap>>* alphas = strokeAlphas_;
    fns.capture = [alphas](const ChunkKey& key, ChunkSnapshot& out, uint32_t flags) {
        if (!(flags & AF_Alpha) || !alphas) return;
        const size_t i = static_cast<size_t>(key.chunkIndex);
        if (i >= alphas->size()) return;
        std::vector<std::vector<uint8_t>> layers;
        layers.reserve((*alphas)[i].size());
        for (const AlphaMap& m : (*alphas)[i])
            layers.emplace_back(m.texels.begin(), m.texels.end());
        out.alphaLayers = std::move(layers);
    };
    return fns;
}

int TexturePaintPanel::paint(std::vector<std::vector<AlphaMap>>& chunkAlphas,
                             const std::vector<MapChunk>& chunks,
                             int blockX, int blockY, Vec3 center) {
    if (layer_ < 1) return 0;
    beginStrokeIfNeeded(chunkAlphas);

    const size_t  slot   = static_cast<size_t>(layer_ - 1);   // index into chunkAlphas[c]
    const uint8_t target = erase_ ? 0u
        : static_cast<uint8_t>(std::clamp(targetAlpha_, 0, 255));
    // Legacy strength_ multiplies the airbrush pressure so pre-existing
    // configurations keep their exact rate (pressure_ defaults to 1).
    const float rate  = std::clamp(strength_ * pressure_, 0.0f, 1.0f);
    const float CH    = static_cast<float>(CHUNK_SIZE);
    const float radUV = radius_ / CH;
    // Spray dabs land up to outer+dab radius from the centre.
    const float reachUV = spray_ ? (sprayOuterRadius_ + sprayDabRadius_) / CH
                                 : radUV;
    const int   fi   = (falloff_ >= 0 && falloff_ < kFalloffCount) ? falloff_ : 0;
    const Falloff fo = kFalloffs[fi];
    const CaptureFns cap = alphaCapture();

    int hits = 0;
    uint32_t advancedRng = sprayRng_;
    bool     sprayed     = false;
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
        // Cheap reject: brush can't reach a chunk whose UV box is beyond reach.
        if (u < -reachUV || u > 1.0f + reachUV ||
            v < -reachUV || v > 1.0f + reachUV) continue;
        // COW pre-snapshot before the first texel of the stroke lands here.
        if (strokeOpen_)
            stack_->touchChunk(ChunkKey{ blockX, blockY, static_cast<int>(c) }, cap);
        if (spray_) {
            // Every chunk replays the SAME dab offsets (fresh copy of the RNG
            // state) so dabs straddling a seam stay aligned across chunks; the
            // session state advances once per stroke step.
            uint32_t rng = sprayRng_;
            hits += sprayAlpha(chunkAlphas[c][slot], u, v,
                               sprayOuterRadius_ / CH, sprayDabRadius_ / CH,
                               sprayDabsPerStep_, rng, rate, fo, target);
            advancedRng = rng;
            sprayed     = true;
        } else {
            hits += paintAlpha(chunkAlphas[c][slot], u, v, radUV,
                               hardness_, rate, fo, target);
        }
    }
    if (sprayed) sprayRng_ = advancedRng;
    return hits;
}

void TexturePaintPanel::endStroke() {
    if (stack_ && strokeOpen_) stack_->commit(alphaCapture());
    strokeOpen_   = false;
    strokeAlphas_ = nullptr;
}

void TexturePaintPanel::draw() {
    ImGui::Begin("Texture Paint");

    ImGui::Checkbox("Paint on drag", &enabled_);
    ImGui::SliderInt("Layer", &layer_, 1, 3);
    ImGui::Checkbox("Erase", &erase_);
    ImGui::SliderFloat("Radius", &radius_, 1.0f, 100.0f);
    ImGui::SliderFloat("Strength", &strength_, 0.0f, 1.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Applied once PER STROKE STEP (each brush application"
                          " while dragging),\nnot per second.");
    ImGui::Combo("Falloff", &falloff_, kFalloffNames, kFalloffCount);

    ImGui::SeparatorText("Airbrush");
    ImGui::SliderFloat("Hardness", &hardness_, 0.0f, 1.0f);
    ImGui::SliderFloat("Pressure", &pressure_, 0.0f, 1.0f);
    ImGui::SliderInt("Target alpha", &targetAlpha_, 0, 255);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Alt+1..5: 255 / 191 / 127 / 63 / 0");
    ImGui::Checkbox("Spray", &spray_);
    if (spray_) {
        ImGui::SliderFloat("Outer radius", &sprayOuterRadius_, 1.0f, 100.0f);
        ImGui::SliderFloat("Dab radius", &sprayDabRadius_, 0.5f, 20.0f);
        ImGui::SliderInt("Dabs per step", &sprayDabsPerStep_, 1, 32);
    }

    // Alt+1..5 opacity hotkeys (pure state change; also callable by the host).
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyAlt)
        for (int i = 0; i < 5; ++i)
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false))
                setOpacityHotkey(i + 1);

    ImGui::End();
}

} // namespace wf::editor
