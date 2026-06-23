#include "test.hpp"
#include "world_view.hpp"
#include "editor_bridge.hpp"
#include "debugdraw.hpp"

#include <vector>

using namespace wf;

namespace {
EntityState mkState(uint64_t guid, uint8_t kind, Vec3 pos, bool moving = true) {
    EntityState e; e.guid = guid; e.kind = kind; e.entry = 100 + (uint32_t)guid;
    e.mapId = 0; e.pos = pos; e.orientation = 0.0f; e.moving = moving; e.speed = 5.0f;
    return e;
}
} // namespace

void test_world_view() {
    std::printf("[world_view]\n");

    WorldView view;
    CHECK(view.count() == 0);

    // --- ingest via decoded frames (the real path off BridgeClient::poll) ----
    EditorFrame f; size_t used = 0;
    CHECK(readFrame(encode(mkState(10, 0, {0,0,0})), f, used));
    view.onFrame(f, /*nowMs*/100);
    CHECK(view.count() == 1);
    const LiveEntity* le = view.find(10);
    CHECK(le && le->updates == 1 && le->state.kind == 0);
    CHECK_APPROX(le->state.pos.x, 0.0f);

    // A second state for the same guid updates position + tracks prevPos.
    view.apply(mkState(10, 0, {5,0,0}), 200);
    le = view.find(10);
    CHECK(le->updates == 2);
    CHECK_APPROX(le->state.pos.x, 5.0f);
    CHECK_APPROX(le->prevPos.x, 0.0f);            // previous position retained
    CHECK(le->lastSeenMs == 200);

    // --- multiple kinds + counts --------------------------------------------
    view.apply(mkState(20, 1, {10,0,0}), 200);    // a player
    view.apply(mkState(30, 2, {0,10,0}, false), 200);   // a gameobject, idle
    WorldView::Counts c = view.counts();
    CHECK(c.total == 3);
    CHECK(c.creatures == 1 && c.players == 1 && c.gameObjects == 1);
    CHECK(c.moving == 2);                          // the idle gameobject excluded

    // --- GUID-sorted snapshot -----------------------------------------------
    std::vector<LiveEntity> list = view.entities();
    CHECK(list.size() == 3);
    CHECK(list[0].state.guid == 10 && list[1].state.guid == 20 && list[2].state.guid == 30);

    // --- selection ----------------------------------------------------------
    CHECK(!view.hasSelection());
    view.select(20);
    CHECK(view.hasSelection() && view.selected() == 20);

    // --- remove via frame, and selection clears when its target leaves -------
    CHECK(readFrame(encode(EntityRemove{20}), f, used));
    view.onFrame(f);
    CHECK(view.count() == 2 && view.find(20) == nullptr);
    CHECK(!view.hasSelection());                   // selected entity was removed

    // --- staleness prune ----------------------------------------------------
    // guid 10 last seen at 200, guid 30 at 200. Advance the clock and prune.
    view.apply(mkState(10, 0, {6,0,0}), 1000);     // refresh 10 only
    size_t pruned = view.prune(/*now*/1500, /*maxAge*/600);
    CHECK(pruned == 1);                            // guid 30 (last seen 200) dropped
    CHECK(view.count() == 1 && view.find(10) != nullptr);

    // --- buildDebug emits renderable primitives for each entity --------------
    view.apply(mkState(40, 1, {3,3,0}), 1500);
    view.select(40);
    DebugDraw dd;
    view.buildDebug(dd);
    // Two entities -> at least two marker points, plus arrows/crosses (lines).
    CHECK(dd.categoryBuffers(DebugCategory::Marker).points.size() >= 2);
    CHECK(!dd.categoryBuffers(DebugCategory::Marker).lines.empty());

    // Colours distinguish kinds.
    CHECK(WorldView::kindColor(1).b > WorldView::kindColor(0).b);   // player bluer

    // An entity with a model box draws its oriented box (extra edges).
    {
        WorldView vbox; EntityState eb; eb.guid = 1; eb.pos = {0,0,0};
        eb.aabbMin = {-1,-1,-1}; eb.aabbMax = {1,1,1};
        vbox.apply(eb);
        DebugDraw db; vbox.buildDebug(db);
        WorldView vno; EntityState en; en.guid = 2; en.pos = {0,0,0};
        vno.apply(en);                         // no box
        DebugDraw dn; vno.buildDebug(dn);
        CHECK(db.categoryBuffers(DebugCategory::Marker).lines.size() >
              dn.categoryBuffers(DebugCategory::Marker).lines.size());
    }

    // --- server status ingest via frame -------------------------------------
    CHECK(!view.hasServerStatus());
    ServerStatus ss; ss.simTimeMs = 5000; ss.entityCount = 9; ss.weatherType = 1;
    ss.weatherGrade = 0.4f; ss.weatherCount = 2;
    CHECK(readFrame(encode(ss), f, used));
    view.onFrame(f);
    CHECK(view.hasServerStatus());
    CHECK(view.serverStatus().simTimeMs == 5000);
    CHECK(view.serverStatus().entityCount == 9);
    CHECK(view.serverStatus().weatherType == 1 && view.serverStatus().weatherCount == 2);
}
