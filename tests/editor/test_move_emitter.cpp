#include "test.hpp"
#include "editor/MoveEmitter.hpp"
#include "editor_bridge.hpp"

using namespace wf;
using namespace wf::editor;

void test_move_emitter() {
    std::printf("[editor.move_emitter]\n");

    MoveEmitter em;

    // Idle: no gizmo, no op.
    CHECK(!em.update(false, 5, {0,0,0}, 0.0f).has_value());

    // A drag of entity 5 from origin to (10,0,0): nothing until release.
    CHECK(!em.update(true, 5, {0,0,0}, 0.0f).has_value());     // begin
    CHECK(em.dragging());
    CHECK(!em.update(true, 5, {4,0,0}, 0.0f).has_value());     // mid-drag
    auto op = em.update(false, 5, {10,0,0}, 1.25f);            // release -> commit
    CHECK(op.has_value());
    CHECK(op->guid == 5);
    CHECK_APPROX(op->pos.x, 10.0f);
    CHECK_APPROX(op->orientation, 1.25f);
    CHECK(op->opId == 1);
    CHECK(!em.dragging());

    // A second drag increments the opId.
    em.update(true, 9, {0,0,0}, 0.0f);
    auto op2 = em.update(false, 9, {0,5,0}, 0.0f);
    CHECK(op2.has_value() && op2->guid == 9 && op2->opId == 2);
    CHECK_APPROX(op2->pos.y, 5.0f);

    // A drag that doesn't actually move emits nothing.
    em.update(true, 3, {1,1,1}, 0.0f);
    CHECK(!em.update(false, 3, {1,1,1}, 0.0f).has_value());

    // A static selection (guid 0) never emits, even if "moved".
    em.update(true, 0, {0,0,0}, 0.0f);
    CHECK(!em.update(false, 0, {20,20,20}, 0.0f).has_value());

    // The guid committed is the one the drag STARTED on (selection can't be
    // swapped mid-drag into a different move).
    em.update(true, 100, {0,0,0}, 0.0f);
    auto op3 = em.update(false, 200, {3,0,0}, 0.0f);
    CHECK(op3.has_value() && op3->guid == 100);
}
