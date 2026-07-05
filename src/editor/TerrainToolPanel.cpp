#include "editor/TerrainToolPanel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "coords.hpp"    // chunkCornerWorld, CHUNK_SIZE
#include "editing.hpp"
#include "imgui.h"

namespace wf::editor {

// The editing.hpp Falloff profiles, in the order the UI dropdown presents them.
// (Falloff::Flat, Linear, Smooth, Gaussian, Polynomial, Trigonometric,
// Quadratic.) Indices are serialised in saved settings; only APPEND.
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

static const char* strokeLabel(TerrainToolPanel::Mode m) {
    switch (m) {
        case TerrainToolPanel::Mode::Raise:   return "Raise terrain";
        case TerrainToolPanel::Mode::Lower:   return "Lower terrain";
        case TerrainToolPanel::Mode::Flatten: return "Flatten terrain";
        case TerrainToolPanel::Mode::Smooth:  return "Smooth terrain";
    }
    return "Sculpt terrain";
}

// Build the editing.hpp Brush from the panel's current UI state, centred at
// `center`. Shared by the procedural-mesh and source-chunk apply paths.
Brush TerrainToolPanel::makeBrush(Vec3 center) const {
    Brush b;
    b.center     = center;
    b.radius     = radius_;
    b.strength   = strength_;
    b.innerRatio = 0.0f;
    const int fi = (falloff_ >= 0 && falloff_ < kFalloffCount) ? falloff_ : 0;
    b.falloff    = kFalloffs[fi];
    return b;
}

FlattenPlane TerrainToolPanel::makePlane(Vec3 center, float fallbackZ) const {
    FlattenPlane plane;
    plane.lock           = useLock_ ? lockPoint_ : Vec3{ center.x, center.y, fallbackZ };
    plane.orientationDeg = orientationDeg_;
    plane.angleDeg       = angleDeg_;
    return plane;
}

int TerrainToolPanel::apply(Mesh& mesh, Vec3 center) const {
    Brush b = makeBrush(center);

    switch (static_cast<Mode>(mode_)) {
        case Mode::Raise:
            return brushRaiseLower(mesh.vertices, b, +1.0f);
        case Mode::Lower:
            return brushRaiseLower(mesh.vertices, b, -1.0f);
        case Mode::Flatten: {
            // Fallback targetZ = the Z of the mesh vertex nearest `center` (in
            // XY); with the default level plane this reproduces the classic
            // flatten exactly.
            float bestD2 = std::numeric_limits<float>::max();
            float targetZ = center.z;
            for (const Vertex& v : mesh.vertices) {
                const float dx = v.position.x - center.x;
                const float dy = v.position.y - center.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; targetZ = v.position.z; }
            }
            const FlattenMode fm = static_cast<FlattenMode>(
                std::clamp(flattenMode_, 0, 2));
            return brushFlatten(mesh.vertices, b, makePlane(center, targetZ), fm);
        }
        case Mode::Smooth:
            // No mesh-vertex smooth core; the source-chunk path (applyChunks)
            // is the authoritative sculpting surface for it.
            return 0;
    }
    return 0;
}

// Open the stroke's single undo action on the FIRST apply of a contiguous
// drag; every step then COW-snapshots the chunks it reaches (touchChunk).
void TerrainToolPanel::beginStrokeIfNeeded(std::vector<MapChunk>& chunks) {
    if (!stack_ || strokeOpen_) return;
    stack_->begin(AF_Heights, strokeLabel(static_cast<Mode>(mode_)),
                  MergeMode::Disable);
    strokeChunks_ = &chunks;
    strokeOpen_   = true;
}

// Copy-on-write pre-snapshot for every chunk the brush circle reaches: the
// brush touches a chunk when the circle intersects its world-XY footprint
// (chunkCornerWorld is the NW corner; the chunk extends -CHUNK_SIZE in both
// axes). Conservative by design -- an untouched-in-the-end chunk just stores
// pre == post, which undo re-applies harmlessly.
void TerrainToolPanel::touchChunksInRange(const std::vector<MapChunk>& chunks,
                                          int blockX, int blockY, Vec3 center) {
    if (!stack_ || !strokeOpen_) return;
    const float CH = static_cast<float>(CHUNK_SIZE);
    const CaptureFns cap = heightCapture();
    for (size_t c = 0; c < chunks.size(); ++c) {
        const MapChunk& mc = chunks[c];
        const Vec3 corner = chunkCornerWorld(blockX, blockY,
                                             static_cast<int>(mc.indexY),
                                             static_cast<int>(mc.indexX), 0.0f);
        const float nx = std::clamp(center.x, corner.x - CH, corner.x);
        const float ny = std::clamp(center.y, corner.y - CH, corner.y);
        const float dx = center.x - nx;
        const float dy = center.y - ny;
        if (dx * dx + dy * dy > radius_ * radius_) continue;
        stack_->touchChunk(ChunkKey{ blockX, blockY, static_cast<int>(c) }, cap);
    }
}

CaptureFns TerrainToolPanel::heightCapture() const {
    CaptureFns fns;
    const std::vector<MapChunk>* chunks = strokeChunks_;
    fns.capture = [chunks](const ChunkKey& key, ChunkSnapshot& out, uint32_t flags) {
        if (!(flags & AF_Heights) || !chunks) return;
        const size_t i = static_cast<size_t>(key.chunkIndex);
        if (i < chunks->size()) out.heights = (*chunks)[i].heights;
    };
    return fns;
}

int TerrainToolPanel::applyChunks(std::vector<MapChunk>& chunks, int blockX,
                                  int blockY, Vec3 center) {
    beginStrokeIfNeeded(chunks);
    touchChunksInRange(chunks, blockX, blockY, center);

    Brush b = makeBrush(center);
    switch (static_cast<Mode>(mode_)) {
        case Mode::Raise:   return brushRaiseLowerChunks(chunks, blockX, blockY, b, +1.0f);
        case Mode::Lower:   return brushRaiseLowerChunks(chunks, blockX, blockY, b, -1.0f);
        case Mode::Flatten: {
            const FlattenMode fm = static_cast<FlattenMode>(
                std::clamp(flattenMode_, 0, 2));
            return brushFlattenChunks(chunks, blockX, blockY, b,
                                      makePlane(center, center.z), fm);
        }
        case Mode::Smooth:  return brushSmoothChunks(chunks, blockX, blockY, b);
    }
    return 0;
}

void TerrainToolPanel::endStroke() {
    if (stack_ && strokeOpen_) stack_->commit(heightCapture());
    strokeOpen_   = false;
    strokeChunks_ = nullptr;
}

void TerrainToolPanel::draw() {
    ImGui::Begin("Terrain Tool");

    ImGui::Checkbox("Sculpt on drag", &enabled_);

    const char* modes[] = { "Raise", "Lower", "Flatten", "Smooth" };
    ImGui::Combo("Mode", &mode_, modes, IM_ARRAYSIZE(modes));

    ImGui::SliderFloat("Radius", &radius_, 1.0f, 100.0f);
    ImGui::SliderFloat("Strength", &strength_, 0.0f, 10.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Applied once PER STROKE STEP (each brush application"
                          " while dragging),\nnot per second -- drag speed changes"
                          " how often it lands, not how much.");

    ImGui::Combo("Falloff", &falloff_, kFalloffNames, kFalloffCount);

    if (static_cast<Mode>(mode_) == Mode::Flatten) {
        ImGui::SeparatorText("Flatten");
        ImGui::RadioButton("Both", &flattenMode_, 0); ImGui::SameLine();
        ImGui::RadioButton("Raise only", &flattenMode_, 1); ImGui::SameLine();
        ImGui::RadioButton("Lower only", &flattenMode_, 2);
        ImGui::SliderFloat("Orientation", &orientationDeg_, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Angle", &angleDeg_, 0.0f, 89.0f, "%.0f deg");
        ImGui::Checkbox("Use lock point", &useLock_);
        ImGui::InputFloat3("Lock", &lockPoint_.x);
    }

    ImGui::End();
}

} // namespace wf::editor
