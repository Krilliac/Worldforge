#include "editor/DebugVisPanel.hpp"

#include "imgui.h"

namespace wf::editor {

const char* debugCategoryName(DebugCategory c) {
    switch (c) {
        case DebugCategory::Generic:     return "Generic";
        case DebugCategory::TerrainWire: return "Terrain wireframe";
        case DebugCategory::DoodadWire:  return "Doodad wireframe";
        case DebugCategory::Collision:   return "Collision";
        case DebugCategory::Waypoint:    return "Waypoints";
        case DebugCategory::NavMesh:     return "Nav mesh";
        case DebugCategory::NavPath:     return "Nav path";
        case DebugCategory::Trigger:     return "Triggers";
        case DebugCategory::Marker:      return "Markers";
        case DebugCategory::Normal:      return "Normals";
        case DebugCategory::Grid:        return "Grid";
        case DebugCategory::Frustum:     return "Frustum";
        case DebugCategory::Cell:        return "Cells";
        case DebugCategory::LineOfSight: return "Line of sight";
        case DebugCategory::HitPoint:    return "Hit points";
        case DebugCategory::Height:      return "Height";
        case DebugCategory::Count:       break;
    }
    return "?";
}

void DebugVisPanel::draw(DebugDraw& dd) {
    ImGui::Begin("Debug Visualisation");

    for (uint32_t i = 0; i < static_cast<uint32_t>(DebugCategory::Count); ++i) {
        DebugCategory c = static_cast<DebugCategory>(i);
        bool on = dd.categoryEnabled(c);
        if (ImGui::Checkbox(debugCategoryName(c), &on))
            dd.setCategoryEnabled(c, on);
    }

    ImGui::Separator();
    DebugDraw::Stats s = dd.stats();
    ImGui::Text("Lines: %d   Tris: %d   Points: %d", s.lines, s.triangles, s.points);

    ImGui::End();
}

} // namespace wf::editor
