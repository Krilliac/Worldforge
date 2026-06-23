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

#include "world_view.hpp"

namespace wf::editor {

class EntityInspectorPanel {
public:
    // Draw the inspector against the live mirror. Sets view.select() when a row
    // is clicked; returns the currently-selected GUID (0 if none).
    uint64_t draw(WorldView& view);

    // ---- filter state (also drives passesFilter, exposed for tests) --------
    bool showCreatures   = true;
    bool showPlayers     = true;
    bool showGameObjects = true;
    bool movingOnly      = false;

    bool passesFilter(const EntityState& s) const;

    // Human-readable kind name (0 creature, 1 player, 2 gameobject).
    static const char* kindName(uint8_t kind);

private:
    int  lastVisible_ = 0;   // rows shown last frame (diagnostics)
};

} // namespace wf::editor
