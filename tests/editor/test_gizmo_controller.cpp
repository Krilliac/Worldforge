#include "test.hpp"
#include "imgui.h"

#include "editor/GizmoController.hpp"
#include "command_stack.hpp"
#include "gizmo.hpp"
#include "math.hpp"

#include <vector>

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

    // --- DragCommitTracker: pure edge detection ------------------------------
    {
        DragCommitTracker tk;
        CHECK(!tk.dragging());
        CHECK(tk.update(true)  == DragCommitTracker::Event::Begin);
        CHECK(tk.update(true)  == DragCommitTracker::Event::None);
        CHECK(tk.dragging());
        CHECK(tk.update(false) == DragCommitTracker::Event::End);
        CHECK(tk.update(false) == DragCommitTracker::Event::None);
        // reset() forgets an in-flight drag without emitting End (cancel path).
        tk.update(true);
        tk.reset();
        CHECK(!tk.dragging());
        CHECK(tk.update(false) == DragCommitTracker::Event::None);
    }

    // --- capture-commit: [true, true, false] -> exactly ONE action -----------
    {
        CommandStack stack;
        GizmoController g;
        g.setCommandStack(&stack);

        Vec3 pos{1, 2, 3}, rot{0, 0, 0};
        float scale = 1.0f;
        std::vector<SelectedPlacement> sel = { { 42, false, &pos, &rot, &scale } };

        g.updateDrag(true, sel);              // rising edge: snapshot + begin
        CHECK(g.dragActive());
        CHECK(stack.isOpen());
        // Undo mid-drag is blocked: the cursor never moves while an action is open.
        CHECK(!stack.undo(ApplyFns{}));
        CHECK(stack.cursor() == 0);

        pos.x = 9.0f;                          // host sets live values during the drag
        g.updateDrag(true, sel);              // mid-drag: nothing pushed
        CHECK(stack.size() == 0);
        g.updateDrag(false, sel);             // falling edge: ONE committed action
        CHECK(!g.dragActive());
        CHECK(!stack.isOpen());
        CHECK(stack.size() == 1);
        CHECK(stack.cursor() == 1);
        CHECK(stack.at(0).transformed.size() == 1);
        CHECK(stack.at(0).transformed[0].uniqueId == 42u);
        CHECK_APPROX(stack.at(0).transformed[0].before.pos.x, 1.0f);
        CHECK_APPROX(stack.at(0).transformed[0].after.pos.x, 9.0f);

        // The RELATIVE repeat delta was latched: +8 in X, no rotation, scale 1.
        CHECK(stack.lastRepeatDelta().valid);
        CHECK_APPROX(stack.lastRepeatDelta().translation.x, 8.0f);
        CHECK_APPROX(stack.lastRepeatDelta().axisAngleDeg.z, 0.0f);
        CHECK_APPROX(stack.lastRepeatDelta().scaleFactor, 1.0f);
    }

    // --- cancelDrag restores bit-exact and records nothing -------------------
    {
        CommandStack stack;
        GizmoController g;
        g.setCommandStack(&stack);

        Vec3 pos{1.5f, -2.25f, 3.75f}, rot{10, 20, 30};
        float scale = 1.25f;
        std::vector<SelectedPlacement> sel = { { 7, true, &pos, &rot, &scale } };

        g.updateDrag(true, sel);
        pos = {99, 99, 99}; rot = {1, 2, 3}; scale = 4.0f;   // the aborted drag
        g.cancelDrag();
        CHECK(!g.dragActive());
        CHECK(!stack.isOpen());
        CHECK(stack.size() == 0);                            // zero actions
        CHECK(pos.x == 1.5f && pos.y == -2.25f && pos.z == 3.75f);   // bit-exact
        CHECK(rot.x == 10.0f && rot.y == 20.0f && rot.z == 30.0f);
        CHECK(scale == 1.25f);

        // Works without a stack too: the restore set is panel-local.
        GizmoController bare;
        bare.updateDrag(true, sel);
        pos.x = -50.0f;
        bare.cancelDrag();
        CHECK(pos.x == 1.5f);
    }

    // --- repeatLast: the RELATIVE delta re-applies to a fresh selection ------
    {
        CommandStack stack;
        GizmoController g;
        g.setCommandStack(&stack);

        Vec3 a{0, 0, 0}, ar{0, 0, 0};
        float as = 1.0f;
        std::vector<SelectedPlacement> selA = { { 1, false, &a, &ar, &as } };
        g.updateDrag(true, selA);
        a.x = 8.0f;                            // 'translate +8,0,0'
        g.updateDrag(false, selA);
        CHECK(stack.size() == 1);

        Vec3 b{100, 50, 0}, br{0, 0, 0};
        float bs = 1.0f;
        std::vector<SelectedPlacement> selB = { { 2, false, &b, &br, &bs } };
        CHECK(g.repeatLast(selB));
        CHECK_APPROX(b.x, 108.0f);             // moved by exactly the +8 delta
        CHECK_APPROX(b.y, 50.0f);
        CHECK(stack.size() == 2);              // one fresh committed action
        CHECK(stack.at(1).transformed.size() == 1);

        // Hammer it (TrenchBroom Ctrl+R row-stamping): the delta stays hot.
        CHECK(g.repeatLast(selB));
        CHECK_APPROX(b.x, 116.0f);
        CHECK(stack.size() == 3);

        // Re-bases after undo: the delta applies off the stack's CURRENT top,
        // never an absolute end state.
        ApplyFns app;
        app.transformObject = [&](const ObjTransformRec& rec, bool toAfter) {
            if (rec.uniqueId != 2) return;
            const ObjTransformState& s = toAfter ? rec.after : rec.before;
            b = s.pos; br = s.rotDeg; bs = s.scale;
        };
        CHECK(stack.undo(app));
        CHECK_APPROX(b.x, 108.0f);
        CHECK(g.repeatLast(selB));
        CHECK_APPROX(b.x, 116.0f);

        // No stack / no valid delta / empty selection are all safe no-ops.
        GizmoController bare;
        CHECK(!bare.repeatLast(selB));
        CommandStack empty;
        GizmoController fresh;
        fresh.setCommandStack(&empty);
        CHECK(!fresh.repeatLast(selB));        // no delta latched yet
        CHECK(!g.repeatLast({}));
    }
}
