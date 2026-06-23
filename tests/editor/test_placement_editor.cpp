#include "test.hpp"
#include "editor/PlacementEditor.hpp"
#include "scene_pick.hpp"
#include "coords.hpp"

using namespace wf;
using namespace wf::editor;

namespace {
WorldPick wmoSel(uint32_t id, size_t index) {
    WorldPick p; p.kind = WorldPick::Kind::Wmo; p.uniqueId = id; p.index = index; return p;
}
} // namespace

void test_placement_editor() {
    std::printf("[editor.placement_editor]\n");

    PlacementEditor ed;

    // Idle: nothing.
    CHECK(!ed.update(false, WorldPick{}, {0,0,0}).has_value());

    // Drag a selected WMO from A to B -> emits on release with stored coords.
    WorldPick sel = wmoSel(2001, 3);
    CHECK(!ed.update(true, sel, {100,200,50}).has_value());     // begin
    CHECK(ed.dragging());
    CHECK(!ed.update(true, sel, {110,200,50}).has_value());     // mid-drag
    Vec3 dst{120, 210, 55};
    auto e = ed.update(false, sel, dst);                        // release
    CHECK(e.has_value());
    CHECK(e->kind == WorldPick::Kind::Wmo);
    CHECK(e->uniqueId == 2001 && e->index == 3);
    CHECK_APPROX(e->worldPos.x, 120.0f);
    // storedPos is the MDDF/MODF placement coordinate of the new world position.
    Vec3 expect = worldToPlacement(dst);
    CHECK_APPROX(e->storedPos.x, expect.x);
    CHECK_APPROX(e->storedPos.y, expect.y);
    CHECK_APPROX(e->storedPos.z, expect.z);
    CHECK(!ed.dragging());

    // A no-move drag emits nothing.
    ed.update(true, sel, {5,5,5});
    CHECK(!ed.update(false, sel, {5,5,5}).has_value());

    // An entity selection (not a scene object) emits nothing.
    WorldPick ent; ent.kind = WorldPick::Kind::Entity; ent.guid = 42;
    ed.update(true, ent, {0,0,0});
    CHECK(!ed.update(false, ent, {50,0,0}).has_value());

    // No selection -> nothing.
    ed.update(true, WorldPick{}, {0,0,0});
    CHECK(!ed.update(false, WorldPick{}, {9,9,9}).has_value());
}
