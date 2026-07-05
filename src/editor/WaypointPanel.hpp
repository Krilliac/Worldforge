#pragma once
// ---------------------------------------------------------------------------
// WaypointPanel: patrol-path authoring UI, a thin ImGui layer over the pure
// waypoint_path core. Holds the active WaypointPath + selection; the host
// feeds terrain picks into addNodeAt() (click-to-append -- mirrors `.wp add`
// without teleporting a GM around), previews the path live through the
// existing editor_bridge SetWaypoints op, and copies mangos-zero SQL
// (db_export waypointSql) to the clipboard. Storage tier is a radio: this
// spawn only (creature_movement, keyed by GUID) vs all spawns of the entry
// (creature_movement_template).
//
// Marker rendering stays out of the panel: buildMarkers/buildPathLine turn the
// path into the bridge's DebugMarker/DebugPath primitives ("1", "2", ...
// labels + the polyline) which the host applies into the shared DebugDraw
// layer that DebugVisPanel toggles. Blocked segments (validateSegments against
// the host's LoS ray) come back as red overlay paths.
//
// The op/SQL/marker builders are pure and unit-tested; draw() runs under the
// headless ImGui harness like the other panels.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "editor_bridge.hpp"   // SetWaypoints / DebugMarker / DebugPath
#include "waypoint_path.hpp"

namespace wf::editor {

class WaypointPanel {
public:
    // ---- authoring state ----
    WaypointPath path;
    int          selected = -1;        // node index, -1 = none

    uint64_t targetGuid    = 0;        // spawn GUID (preview target + guid tier)
    uint32_t templateEntry = 0;        // creature entry (template tier)
    bool     asTemplate    = false;    // radio: guid tier vs template tier

    // Host wiring. onPreview receives the framed-op struct when "Preview on
    // server" is pressed; losFn is the raster/vmap sight ray for validation
    // (both optional -- the panel degrades gracefully offline).
    std::function<void(const SetWaypoints&)> onPreview;
    std::function<bool(Vec3, Vec3)>          losFn;

    // Viewport click-to-append: the host calls this with the terrain pick.
    // The new node is appended and selected.
    void addNodeAt(const Vec3& p);

    // Pure builders (tested):
    SetWaypoints previewOp();          // consumes the next opId
    std::string  currentSql() const;   // waypointSql for the active tier

    // Draw the panel (node list, per-node fields, tier radio, SQL/preview
    // buttons). Safe headless.
    void draw();

private:
    uint32_t nextOpId_ = 1;
};

// ---- debugdraw feed (pure, consumed by the host's DebugDraw layer) --------

// One numbered marker per node ("1", "2", ...) at the node position.
std::vector<DebugMarker> buildMarkers(const WaypointPath& path);

// The whole path as one polyline (N points).
DebugPath buildPathLine(const WaypointPath& path, uint64_t guid = 0);

// Red 2-point overlay paths for every blocked segment reported by
// validateSegments, drawn on top of the main polyline.
std::vector<DebugPath> buildBlockedOverlays(const WaypointPath& path,
                                            const std::vector<LosResult>& results);

} // namespace wf::editor
