#pragma once
// ---------------------------------------------------------------------------
// GizmoController: wires ImGuizmo (the in-viewport move/rotate/scale handles)
// to this project's math + the gizmo module's Transform. ImGuizmo draws into the
// ImGui draw list and edits a model matrix in place; this wraps it with our
// column-major Mat4 and the editor's tool mode/space. Compiles and runs
// headlessly (it only emits ImGui draw commands) so the matrix helpers are
// unit-tested without a GPU.
// ---------------------------------------------------------------------------
#include "math.hpp"
#include "gizmo.hpp"   // Transform

namespace wf::editor {

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
};

} // namespace wf::editor
