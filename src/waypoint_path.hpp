#pragma once
// ---------------------------------------------------------------------------
// Waypoint path authoring: the pure geometry + metadata model behind the
// editor's patrol-path tool (docs/EDITOR_RESEARCH.md part D.3). A path is an
// ordered node list; every mutation keeps the order canonical so persistence
// can simply re-emit "Point = index + 1" (the mangos-zero creature_movement
// numbering) -- deleting node 0 renumbers everything for free because the SQL
// emitter always rewrites the whole path atomically (db_export.hpp).
//
// Line-of-sight validation is injected: the host wires the raster/vmap ray
// query, tests wire a fake. Checking LoS between consecutive nodes catches
// paths that clip through the ground or a wall before a creature walks them.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "math.hpp"

namespace wf {

// One patrol node. Mirrors a creature_movement row's authorable fields:
// waitTimeMs = ms pause at the node, scriptId -> dbscripts, comment is free
// text for the DB (quoted/escaped by the SQL emitter).
struct WaypointNode {
    Vec3        pos;
    float       orientation = 0.0f;
    uint32_t    waitTimeMs  = 0;      // 0 = no pause
    uint32_t    scriptId    = 0;
    std::string comment;
};

// An ordered patrol path. pathId is the creature GUID or template entry the
// emitter keys the rows on; node i persists as Point i+1.
struct WaypointPath {
    uint32_t                  pathId = 0;
    std::vector<WaypointNode> nodes;
};

// ---- editing ops (all keep node order canonical) --------------------------

// Append a node at the end of the path.
void appendNode(WaypointPath& path, WaypointNode node);

// Insert a node at `p` on the polyline segment nearest to it (point-to-segment
// distance), AFTER that segment's first endpoint -- so clicking near the
// middle of a leg splits that leg. Empty and single-node paths just append.
// Returns the index the node landed at.
size_t insertNodeNearestSegment(WaypointPath& path, const Vec3& p);

// Relocate node i (position only; metadata stays).
void moveNode(WaypointPath& path, size_t i, const Vec3& pos);

// Delete node i. Later nodes shift down -- the implicit renumber is safe
// because waypointSql() re-emits every Point from scratch.
void removeNode(WaypointPath& path, size_t i);

// Reverse the patrol direction (node order flips; metadata travels with its
// node).
void reverse(WaypointPath& path);

// ---- validation ------------------------------------------------------------

// Verdict for the segment starting at node `fromIdx` (i.e. fromIdx -> fromIdx+1).
struct LosResult {
    int  fromIdx = 0;
    bool blocked = false;
};

// Check line of sight along every consecutive segment. `losFn(a, b)` must
// return true when the sight line a->b is CLEAR (the host wires the existing
// raster/vmap ray query; tests inject a fake). Returns one entry per segment,
// in order; paths with fewer than two nodes have no segments.
std::vector<LosResult> validateSegments(const WaypointPath& path,
                                        const std::function<bool(Vec3, Vec3)>& losFn);

} // namespace wf
