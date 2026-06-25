#pragma once
// ---------------------------------------------------------------------------
// OutlinerPanel: the scene hierarchy. It flattens two sources into one clickable
// tree -- the engine's live WorldView (every entity the server is streaming) and
// the currently-loaded tile's placed static objects (M2 doodads from the ADT's
// MDDF, WMOs from its MODF). Clicking an item yields an OutlinerSelection request
// the host applies (driving the shared WorldView/scene selection so the viewport
// highlights it).
//
// buildItems() is a pure flattener (no ImGui), so it is unit-tested headlessly;
// draw() runs under the headless ImGui harness like the other panels.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "world_view.hpp"
#include "asset_loader.hpp"   // TileScene (instances = doodads, wmoInstances)

namespace wf::editor {

using wf::WorldView;
using wf::TileScene;

// One row of the flattened hierarchy.
struct OutlinerItem {
    enum class Kind { Entity, Doodad, Wmo };
    Kind        kind;
    std::string label;     // e.g. "Entity 0xABCD (Unit)", "Doodad #3", "WMO 12345"
    uint64_t    guid = 0;  // Entity: guid; Wmo: uniqueId; Doodad: 0
    size_t      index = 0; // Doodad/Wmo: index into the TileScene vector
};

// A selection request emitted by draw() when a row is clicked this frame.
struct OutlinerSelection {
    enum class Kind { None, Entity, Doodad, Wmo };
    Kind     kind  = Kind::None;
    uint64_t guid  = 0;
    size_t   index = 0;
};

class OutlinerPanel {
public:
    // Pure: flatten a WorldView's entities + a TileScene's doodads/WMOs to a list.
    // Order: one item per live entity (GUID-sorted, as WorldView returns), then --
    // when scene != nullptr -- one per scene.instances (Doodad) and one per
    // scene.wmoInstances (Wmo). Labels are concise and human-readable.
    static std::vector<OutlinerItem> buildItems(const WorldView& view, const TileScene* scene);

    // Draw the tree (window "Outliner"); returns a selection when an item is
    // clicked this frame (kind None otherwise).
    OutlinerSelection draw(const WorldView& view, const TileScene* scene);

    // Human-readable kind name for an entity (0 creature, 1 player, 2 gameobject).
    static const char* entityKindName(uint8_t kind);
};

} // namespace wf::editor
