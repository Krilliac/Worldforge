#pragma once
// ---------------------------------------------------------------------------
// EntityInspectorPanel: the runtime-data inspector. It lists every object in
// the engine's live WorldView (mirrored from the server's entity stream) in a
// filterable table -- GUID, kind, entry, map, position, moving -- and shows a
// detail pane for the selected object (all of its last-reported fields plus the
// view's bookkeeping: update count, last-seen). Clicking a row drives the shared
// WorldView selection, so the viewport highlights it and the gizmo can grab it.
// This is "data/debug inspectors for runtime data, from the server and from the
// engine" in one panel: the WorldView is both.
//
// The filtering + formatting are pure helpers (unit-tested); draw() runs under
// the headless ImGui harness like the other panels.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "world_view.hpp"
#include "scene_pick.hpp"     // WorldPick (a selected static scene object)
#include "editor_bridge.hpp"  // SpawnCreature / Despawn

namespace wf::editor {

class EntityInspectorPanel {
public:
    // Draw the inspector against the live mirror. Sets view.select() when a row
    // is clicked; returns the currently-selected GUID (0 if none). When
    // `sceneSel` points at a picked static object (terrain/doodad/WMO), a "Scene
    // object" detail section is shown for it. When `ops` is given, an authoring
    // section lets you spawn a creature at the picked point and despawn the
    // selected entity, pushing the framed ops to send.
    uint64_t draw(WorldView& view, const WorldPick* sceneSel = nullptr,
                  std::vector<std::vector<uint8_t>>* ops = nullptr);

    // Op builders (pure, tested): spawn the configured entry/map at `at`, and
    // despawn `guid`. Each consumes the next opId.
    SpawnCreature spawnOp(const Vec3& at);
    Despawn       despawnOp(uint64_t guid);

    // ---- authoring state ----
    int spawnEntry = 1;
    int spawnMapId = 0;

    // Human-readable name for a picked scene-object kind.
    static const char* sceneKindName(WorldPick::Kind kind);

    // ---- filter state (also drives passesFilter, exposed for tests) --------
    bool showCreatures   = true;
    bool showPlayers     = true;
    bool showGameObjects = true;
    bool movingOnly      = false;

    bool passesFilter(const EntityState& s) const;

    // Human-readable kind name (0 creature, 1 player, 2 gameobject).
    static const char* kindName(uint8_t kind);

private:
    int      lastVisible_ = 0;   // rows shown last frame (diagnostics)
    uint32_t nextOpId_    = 1;   // authoring op id sequence
};

} // namespace wf::editor
