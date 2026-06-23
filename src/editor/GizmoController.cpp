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

} // namespace wf::editor
