#include "editor/TerrainToolPanel.hpp"

#include <cmath>
#include <limits>

#include "editing.hpp"
#include "imgui.h"

namespace wf::editor {

// The editing.hpp Falloff profiles, in the order the UI dropdown presents them.
// (Falloff::Flat, Linear, Smooth, Gaussian.)
static const Falloff kFalloffs[] = {
    Falloff::Flat, Falloff::Linear, Falloff::Smooth, Falloff::Gaussian,
};
static const char* kFalloffNames[] = { "Flat", "Linear", "Smooth", "Gaussian" };
static constexpr int kFalloffCount = 4;

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

int TerrainToolPanel::apply(Mesh& mesh, Vec3 center) const {
    Brush b = makeBrush(center);

    switch (static_cast<Mode>(mode_)) {
        case Mode::Raise:
            return brushRaiseLower(mesh.vertices, b, +1.0f);
        case Mode::Lower:
            return brushRaiseLower(mesh.vertices, b, -1.0f);
        case Mode::Flatten: {
            // targetZ = the Z of the mesh vertex nearest `center` (in XY).
            float bestD2 = std::numeric_limits<float>::max();
            float targetZ = center.z;
            for (const Vertex& v : mesh.vertices) {
                const float dx = v.position.x - center.x;
                const float dy = v.position.y - center.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; targetZ = v.position.z; }
            }
            return brushFlatten(mesh.vertices, b, targetZ);
        }
    }
    return 0;
}

int TerrainToolPanel::applyChunks(std::vector<MapChunk>& chunks, int blockX,
                                  int blockY, Vec3 center) const {
    Brush b = makeBrush(center);
    switch (static_cast<Mode>(mode_)) {
        case Mode::Raise:   return brushRaiseLowerChunks(chunks, blockX, blockY, b, +1.0f);
        case Mode::Lower:   return brushRaiseLowerChunks(chunks, blockX, blockY, b, -1.0f);
        case Mode::Flatten: return brushFlattenChunks(chunks, blockX, blockY, b, center.z);
    }
    return 0;
}

void TerrainToolPanel::draw() {
    ImGui::Begin("Terrain Tool");

    ImGui::Checkbox("Sculpt on drag", &enabled_);

    const char* modes[] = { "Raise", "Lower", "Flatten" };
    ImGui::Combo("Mode", &mode_, modes, IM_ARRAYSIZE(modes));

    ImGui::SliderFloat("Radius", &radius_, 1.0f, 100.0f);
    ImGui::SliderFloat("Strength", &strength_, 0.0f, 10.0f);

    ImGui::Combo("Falloff", &falloff_, kFalloffNames, kFalloffCount);

    ImGui::End();
}

} // namespace wf::editor
