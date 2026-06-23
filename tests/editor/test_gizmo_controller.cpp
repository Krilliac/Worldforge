#include "test.hpp"
#include "imgui.h"

#include "editor/GizmoController.hpp"
#include "gizmo.hpp"
#include "math.hpp"

using namespace wf;
using namespace wf::editor;

void test_gizmo_controller() {
    std::printf("[editor.gizmo]\n");

    // --- modelFromTransform bakes translation into the matrix ---------------
    Transform t;
    t.position = { 5.0f, -3.0f, 2.0f };
    Mat4 m = GizmoController::modelFromTransform(t);
    // Column-major translate column is m[12..14].
    CHECK_APPROX(m.at(0, 3), 5.0f);
    CHECK_APPROX(m.at(1, 3), -3.0f);
    CHECK_APPROX(m.at(2, 3), 2.0f);

    // --- decompose round-trips translation + scale --------------------------
    Transform s;
    s.position = { 1.0f, 2.0f, 3.0f };
    s.scale    = { 2.0f, 2.0f, 2.0f };
    Mat4 sm = GizmoController::modelFromTransform(s);
    Vec3 tr, euler, sc;
    GizmoController::decompose(sm, tr, euler, sc);
    CHECK_APPROX(tr.x, 1.0f); CHECK_APPROX(tr.y, 2.0f); CHECK_APPROX(tr.z, 3.0f);
    CHECK_APPROX(sc.x, 2.0f); CHECK_APPROX(sc.y, 2.0f); CHECK_APPROX(sc.z, 2.0f);

    // --- headless: ImGuizmo runs in a frame without a mouse drag ------------
    GizmoController giz;
    giz.op = GizmoController::Op::Rotate;
    giz.space = GizmoController::Space::Local;

    Mat4 view = Mat4::lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj = Mat4::perspective(60.0, 1.0, 0.1, 100.0);
    Mat4 model = GizmoController::modelFromTransform(t);

    ImGui::NewFrame();
    giz.beginFrame(0.0f, 0.0f, 1280.0f, 720.0f);
    bool using_ = giz.manipulate(view, proj, model);
    ImGui::Render();

    CHECK(!using_);                          // no mouse interaction -> not dragging
    CHECK(ImGui::GetDrawData()->Valid);      // produced valid geometry
    // With no drag the model matrix translation is unchanged.
    CHECK_APPROX(model.at(0, 3), 5.0f);
}
