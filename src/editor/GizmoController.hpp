#pragma once
// ---------------------------------------------------------------------------
// GizmoController: wires ImGuizmo (the in-viewport move/rotate/scale handles)
// to this project's math + the gizmo module's Transform. ImGuizmo draws into the
// ImGui draw list and edits a model matrix in place; this wraps it with our
// column-major Mat4 and the editor's tool mode/space. Compiles and runs
// headlessly (it only emits ImGui draw commands) so the matrix helpers are
// unit-tested without a GPU.
//
// Undo wiring follows the Godot-style capture-commit protocol: NOTHING is
// pushed onto the command stack while a drag is in flight. The rising edge of
// manipulate()'s true-while-dragging return snapshots the selection (the
// restore set) and opens one AF_ObjTransformed action; the drag itself only
// sets live values; the falling edge records before/after per placement and
// commits exactly ONE action. Escape (cancelDrag) restores the snapshot and
// cancels -- no action is ever recorded. The edge detection lives in the pure
// DragCommitTracker so tests can script true/true/false sequences headlessly.
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"
#include "gizmo.hpp"           // Transform
#include "command_stack.hpp"   // CommandStack, ObjTransformState, RepeatDelta

namespace wf::editor {

// Live handles onto one selected placement's editable transform values. The
// pointers alias the host's placement storage and must stay valid for the
// whole drag (or repeatLast call); the controller reads them at the drag
// edges and writes them back on cancel/repeat.
struct SelectedPlacement {
    uint32_t uniqueId = 0;
    bool     isWmo    = false;
    Vec3*    pos      = nullptr;   // world space
    Vec3*    rotDeg   = nullptr;   // per-axis degrees (MDDF/MODF convention)
    float*   scale    = nullptr;   // uniform factor
};

// Pure rising/falling-edge detector behind the capture-commit protocol. Feed
// it the per-frame "is dragging" flag: the first true frame is Begin, the
// first false frame after a drag is End, everything else is None. reset()
// forgets an in-flight drag without emitting End (the cancel path).
class DragCommitTracker {
public:
    enum class Event { None, Begin, End };

    Event update(bool dragging) {
        if (dragging && !dragging_) { dragging_ = true;  return Event::Begin; }
        if (!dragging && dragging_) { dragging_ = false; return Event::End; }
        return Event::None;
    }
    void reset() { dragging_ = false; }
    bool dragging() const { return dragging_; }

private:
    bool dragging_ = false;
};

class GizmoController {
public:
    enum class Op    { Translate, Rotate, Scale };
    enum class Space { World, Local };

    Op    op    = Op::Translate;
    Space space = Space::World;
    bool  snap  = false;
    float snapStep = 1.0f;

    // Set up ImGuizmo for this frame over the viewport screen rect (call after
    // ImGui::NewFrame). Uses the background draw list so no window is required.
    void beginFrame(float x, float y, float w, float h);

    // Manipulate a model matrix in place via the active handle. Returns true
    // while the gizmo is being dragged.
    bool manipulate(const Mat4& view, const Mat4& proj, Mat4& model);

    // Build a TRS model matrix from a Transform (translate * rotate * scale).
    static Mat4 modelFromTransform(const Transform& t);

    // Decompose a model matrix into translation / euler degrees / scale
    // (ImGuizmo's component order).
    static void decompose(const Mat4& model, Vec3& translation, Vec3& eulerDeg, Vec3& scale);

    // --- undo wiring (capture-commit-cancel) --------------------------------

    // Nullable: with no stack the controller behaves as before, except that
    // the restore set is still kept so cancelDrag() can restore a drag.
    void setCommandStack(CommandStack* stack) { stack_ = stack; }

    // Feed manipulate()'s dragging flag once per frame together with the
    // current selection. Rising edge: snapshot the selection and open one
    // AF_ObjTransformed action. Mid-drag: nothing touches the stack (the host
    // sets live values only). Falling edge: record before/after for every
    // placement, stash the relative repeat delta (from the first placement),
    // and commit exactly ONE action -- a drag that moved nothing still commits
    // (pre == post records), a drag with an empty selection pushes nothing.
    void updateDrag(bool dragging, const std::vector<SelectedPlacement>& selection);

    // Abort the active drag (host calls on Escape): write the drag-begin
    // snapshot back through the live pointers and cancel the open action.
    // No action is recorded; the host should rebuild its gizmo matrix from
    // the restored values. No-op when no drag is active.
    void cancelDrag();

    bool dragActive() const { return tracker_.dragging(); }

    // TrenchBroom-style repeat-last-transform (Ctrl+R row-stamping): re-apply
    // the last committed RELATIVE delta to `selection` as a fresh committed
    // action. The delta is relative -- never an absolute end state -- so it
    // re-bases naturally off the selection's CURRENT values (i.e. off the
    // stack's current top after an undo). Returns false with no stack, no
    // valid delta, an empty selection, or mid-drag.
    bool repeatLast(const std::vector<SelectedPlacement>& selection);

private:
    // One placement's drag-begin state, kept for cancel-restore and the
    // falling-edge before/after records.
    struct DragSnap {
        SelectedPlacement target;
        ObjTransformState before;
    };

    const char* dragLabel() const;

    CommandStack*         stack_ = nullptr;
    DragCommitTracker     tracker_;
    std::vector<DragSnap> restore_;
};

} // namespace wf::editor
