#pragma once
// ---------------------------------------------------------------------------
// PlacementEditor: the static-object analog of MoveEmitter. Dragging the gizmo
// on a selected scene object (an M2 doodad or a WMO -- ADT placements, not live
// server objects) produces a PlacementEdit on release: the new world transform
// plus the stored MDDF/MODF coordinates the client would persist. Doodads/WMOs
// aren't server objects, so this is an asset-authoring edit (re-write the ADT),
// not a live op -- the editor captures it for an offline write-back tool.
//
// Pure logic (no ImGui), unit-tested headlessly.
// ---------------------------------------------------------------------------
#include <optional>

#include "scene_pick.hpp"   // WorldPick
#include "coords.hpp"       // worldToPlacement
#include "math.hpp"

namespace wf::editor {

struct PlacementEdit {
    WorldPick::Kind kind     = WorldPick::Kind::None;   // Doodad or Wmo
    uint32_t        uniqueId = 0;     // placement id (WMO; 0 for terrain/none)
    size_t          index    = 0;     // scene instance index
    Vec3            worldPos;         // the new world-space position
    Vec3            storedPos;        // worldToPlacement(worldPos): MDDF/MODF coords
};

class PlacementEditor {
public:
    // Call every frame with the gizmo-active flag, the current scene selection,
    // and the gizmo's transform position. Emits one PlacementEdit on release if
    // a scene object was dragged a non-trivial distance.
    std::optional<PlacementEdit> update(bool gizmoActive, const WorldPick& sel,
                                        const Vec3& pos) {
        std::optional<PlacementEdit> out;
        if (gizmoActive && !dragging_) {                 // drag begin
            dragging_ = true; sel_ = sel; startPos_ = pos;
        } else if (!gizmoActive && dragging_) {          // drag end -> commit
            dragging_ = false;
            if (sel_.isScene() && length(pos - startPos_) > kEps) {
                PlacementEdit e;
                e.kind = sel_.kind; e.uniqueId = sel_.uniqueId; e.index = sel_.index;
                e.worldPos = pos; e.storedPos = worldToPlacement(pos);
                out = e;
            }
            sel_ = WorldPick{};
        }
        return out;
    }

    bool dragging() const { return dragging_; }

private:
    static constexpr float kEps = 0.01f;
    bool      dragging_ = false;
    WorldPick sel_;
    Vec3      startPos_;
};

} // namespace wf::editor
