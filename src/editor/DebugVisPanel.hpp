#pragma once
// ---------------------------------------------------------------------------
// DebugVisPanel: ImGui checkboxes that toggle each DebugDraw category layer
// (terrain/doodad wireframe, collision, waypoints, nav, triggers, cells, LoS,
// hit points, height, normals, grid, frustum) and show the per-frame primitive
// stats. The "show pathing / colliders / triggers / wireframe" controls.
// ---------------------------------------------------------------------------
#include "debugdraw.hpp"

namespace wf::editor {

// Human-readable label for a category (also used by tests).
const char* debugCategoryName(DebugCategory c);

class DebugVisPanel {
public:
    // Render the toggles against a live DebugDraw and report its stats.
    void draw(DebugDraw& dd);
};

} // namespace wf::editor
