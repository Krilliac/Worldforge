#pragma once
// ---------------------------------------------------------------------------
// MoveEmitter: turns a gizmo drag on the selected live entity into a MoveObject
// op for the server -- the "edit the world" half of the live loop. The viewport
// already moves the gizmo's transform while dragging; this watches the drag and,
// on release, emits one MoveObject(guid, finalPos) so the server relocates the
// creature (and connected 1.12.1 clients see it move). A static selection
// (terrain/doodad/WMO -- no live server object) produces nothing.
//
// Pure logic (no ImGui), so the drag->op behaviour is unit-tested headlessly.
// ---------------------------------------------------------------------------
#include <optional>

#include "editor_bridge.hpp"   // MoveObject
#include "math.hpp"

namespace wf::editor {

class MoveEmitter {
public:
    // Call every frame. `gizmoActive` is ViewportPanel::draw's return (true while
    // the gizmo is dragged); `guid` is the selected entity (0 = none / a static
    // object); `pos`/`orientation` are the gizmo's current transform. Returns a
    // MoveObject exactly once -- on release -- if the entity actually moved.
    std::optional<MoveObject> update(bool gizmoActive, uint64_t guid,
                                     const Vec3& pos, float orientation) {
        std::optional<MoveObject> out;
        if (gizmoActive && !dragging_) {                 // drag begin
            dragging_ = true; guid_ = guid; startPos_ = pos;
        } else if (!gizmoActive && dragging_) {          // drag end -> commit
            dragging_ = false;
            if (guid_ != 0 && length(pos - startPos_) > kEps) {
                MoveObject m;
                m.guid = guid_; m.pos = pos; m.orientation = orientation;
                m.opId = ++lastOpId_;
                out = m;
            }
            guid_ = 0;
        }
        return out;
    }

    bool     dragging() const { return dragging_; }
    uint32_t lastOpId() const { return lastOpId_; }

private:
    static constexpr float kEps = 0.01f;   // ignore sub-cm jitter
    bool     dragging_ = false;
    uint64_t guid_     = 0;
    Vec3     startPos_;
    uint32_t lastOpId_ = 0;
};

} // namespace wf::editor
