#include "editor/GizmoController.hpp"

#include "imgui.h"
#include "ImGuizmo.h"

namespace wf::editor {

namespace {
ImGuizmo::OPERATION toOp(GizmoController::Op o) {
    switch (o) {
        case GizmoController::Op::Translate: return ImGuizmo::TRANSLATE;
        case GizmoController::Op::Rotate:    return ImGuizmo::ROTATE;
        case GizmoController::Op::Scale:     return ImGuizmo::SCALE;
    }
    return ImGuizmo::TRANSLATE;
}

// Read a placement's current transform values through its live pointers
// (null members fall back to identity components).
ObjTransformState stateOf(const SelectedPlacement& s) {
    ObjTransformState st;
    if (s.pos)    st.pos    = *s.pos;
    if (s.rotDeg) st.rotDeg = *s.rotDeg;
    if (s.scale)  st.scale  = *s.scale;
    return st;
}
} // namespace

void GizmoController::beginFrame(float x, float y, float w, float h) {
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(x, y, w, h);
}

bool GizmoController::manipulate(const Mat4& view, const Mat4& proj, Mat4& model) {
    ImGuizmo::MODE mode = (space == Space::World) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    float snapVec[3] = { snapStep, snapStep, snapStep };
    ImGuizmo::Manipulate(view.m.data(), proj.m.data(), toOp(op), mode,
                         model.m.data(), nullptr, snap ? snapVec : nullptr);
    return ImGuizmo::IsUsing();
}

Mat4 GizmoController::modelFromTransform(const Transform& t) {
    return Mat4::translate(t.position) * t.rotation.toMat4() * Mat4::scale(t.scale);
}

void GizmoController::decompose(const Mat4& model, Vec3& translation, Vec3& eulerDeg, Vec3& scale) {
    float tr[3], rot[3], sc[3];
    ImGuizmo::DecomposeMatrixToComponents(model.m.data(), tr, rot, sc);
    translation = { tr[0], tr[1], tr[2] };
    eulerDeg    = { rot[0], rot[1], rot[2] };
    scale       = { sc[0], sc[1], sc[2] };
}

const char* GizmoController::dragLabel() const {
    switch (op) {
        case Op::Translate: return "Move object";
        case Op::Rotate:    return "Rotate object";
        case Op::Scale:     return "Scale object";
    }
    return "Move object";
}

void GizmoController::updateDrag(bool dragging,
                                 const std::vector<SelectedPlacement>& selection) {
    switch (tracker_.update(dragging)) {
        case DragCommitTracker::Event::Begin: {
            // Rising edge: snapshot the selection's transforms (the restore
            // set) and open ONE action. Nothing is pushed yet.
            restore_.clear();
            restore_.reserve(selection.size());
            for (const SelectedPlacement& s : selection)
                restore_.push_back(DragSnap{ s, stateOf(s) });
            if (stack_)
                stack_->begin(AF_ObjTransformed, dragLabel(), MergeMode::Disable);
            break;
        }
        case DragCommitTracker::Event::End: {
            // Falling edge: before/after per placement, one committed action.
            // The relative delta (translation vec, axis*angle, scale factor)
            // of the PRIMARY (first) placement feeds repeat-last-transform.
            if (stack_) {
                RepeatDelta delta;
                for (size_t i = 0; i < restore_.size(); ++i) {
                    const DragSnap& snap = restore_[i];
                    ObjTransformRec rec;
                    rec.uniqueId = snap.target.uniqueId;
                    rec.isWmo    = snap.target.isWmo;
                    rec.before   = snap.before;
                    rec.after    = stateOf(snap.target);
                    stack_->recordObjTransformed(rec);
                    if (i == 0) {
                        delta.translation  = rec.after.pos - rec.before.pos;
                        delta.axisAngleDeg = rec.after.rotDeg - rec.before.rotDeg;
                        delta.scaleFactor  = rec.before.scale != 0.0f
                                               ? rec.after.scale / rec.before.scale
                                               : 1.0f;
                        delta.valid = true;
                    }
                }
                if (delta.valid) stack_->recordRepeatDelta(delta);
                stack_->commit(CaptureFns{});   // no chunks touched: object-only action
            }
            restore_.clear();
            break;
        }
        case DragCommitTracker::Event::None:
            break;   // mid-drag: live values only, the stack is untouched
    }
}

void GizmoController::cancelDrag() {
    if (!tracker_.dragging()) return;
    // Restore the drag-begin snapshot directly -- no action is recorded.
    for (const DragSnap& snap : restore_) {
        if (snap.target.pos)    *snap.target.pos    = snap.before.pos;
        if (snap.target.rotDeg) *snap.target.rotDeg = snap.before.rotDeg;
        if (snap.target.scale)  *snap.target.scale  = snap.before.scale;
    }
    restore_.clear();
    if (stack_) stack_->cancel();
    tracker_.reset();
}

bool GizmoController::repeatLast(const std::vector<SelectedPlacement>& selection) {
    if (!stack_ || selection.empty() || tracker_.dragging()) return false;
    // Copy: begin()/commit() below rewrite the stack's stored delta in place.
    const RepeatDelta delta = stack_->lastRepeatDelta();
    if (!delta.valid) return false;

    stack_->begin(AF_ObjTransformed, "Repeat transform", MergeMode::Disable);
    for (const SelectedPlacement& s : selection) {
        ObjTransformRec rec;
        rec.uniqueId = s.uniqueId;
        rec.isWmo    = s.isWmo;
        rec.before   = stateOf(s);
        // Apply the RELATIVE delta to the placement's current values.
        if (s.pos)    *s.pos    += delta.translation;
        if (s.rotDeg) *s.rotDeg += delta.axisAngleDeg;
        if (s.scale)  *s.scale  *= delta.scaleFactor;
        rec.after = stateOf(s);
        stack_->recordObjTransformed(rec);
    }
    stack_->recordRepeatDelta(delta);   // keep the delta hot for hammering Ctrl+R
    stack_->commit(CaptureFns{});
    return true;
}

} // namespace wf::editor
