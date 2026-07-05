#pragma once
// ---------------------------------------------------------------------------
// World-DB persistence: turn editor placements into mangos-zero / cMaNGOS world
// SQL so edits survive a server restart (docs/EDITOR_RESEARCH.md part D.3). The
// spawn materialises when its grid next loads -- there is no separate
// registration table, so emitting the row is the whole contract. Strings only;
// no DB driver dependency in-tree.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "math.hpp"
#include "waypoint_path.hpp"

namespace wf {

// `creature` spawn row. guid = spawn GUID (PK), entry -> creature_template.
// MovementType: 0 idle, 1 random (uses spawnDist), 2 waypoint.
struct CreatureSpawn {
    uint32_t guid = 0;
    uint32_t entry = 0;
    uint32_t mapId = 0;
    Vec3     pos;
    float    orientation = 0.0f;
    uint32_t spawnTimeSecs = 300;
    float    spawnDist = 0.0f;
    int      movementType = 0;
};

// `gameobject` spawn row. orientation is also expanded to the quaternion
// rotation0..3 columns (rotation about world +Z) the way mangos stores them.
struct GameObjectSpawn {
    uint32_t guid = 0;
    uint32_t entry = 0;
    uint32_t mapId = 0;
    Vec3     pos;
    float    orientation = 0.0f;
    uint32_t spawnTimeSecs = 300;
    int      state = 1;             // GO_STATE (e.g. 1 = ready/closed)
};

// One INSERT for the `creature` table.
std::string creatureInsert(const CreatureSpawn& c);

// One INSERT for the `gameobject` table (with rotation0..3 from orientation).
std::string gameObjectInsert(const GameObjectSpawn& g);

// `creature_movement` rows for a patrol path (id = creature.guid, point 0..n-1).
// Also returns the UPDATE that flips the spawn's MovementType to 2 (waypoint).
std::string waypointInserts(uint32_t creatureGuid, const std::vector<Vec3>& path,
                            uint32_t waitTimeMs = 0);

// ---------------------------------------------------------------------------
// Waypoint path persistence (WaypointPath -> mangos-zero SQL). Two storage
// tiers, chosen per creature:
//   creature_movement          -- keyed by spawn GUID: this one spawn patrols.
//   creature_movement_template -- keyed by creature ENTRY: every spawn of the
//                                 template shares the patrol.
// Both share the node columns Id, Point (1-based), PositionX/Y/Z, Orientation,
// WaitTime (ms), ScriptId, Comment with PK (Id, Point).
// ---------------------------------------------------------------------------

// Atomic rewrite of a whole path: DELETE FROM <table> WHERE Id = idOrEntry;
// followed by ONE bulk INSERT of every node (never half-applies -- removing or
// reordering nodes just re-emits, so the implicit Point renumber is free).
// idOrEntry is the spawn GUID (asTemplate = false) or the template entry
// (asTemplate = true). Single quotes in Comment are escaped. An empty path
// emits just the DELETE (clears the patrol).
std::string waypointSql(const WaypointPath& path, uint32_t idOrEntry, bool asTemplate);

// UPDATE creature SET MovementType = <movementType> for one spawn
// (0 idle, 1 random, 2 waypoint). When switching AWAY from waypoint motion the
// spawn's creature_movement rows are deleted too -- unless `nodel` is set (the
// `.npc set movetype ... NODEL` semantic: keep the path rows for later reuse).
std::string movementTypeUpdateSql(uint32_t guid, int movementType, bool nodel);

} // namespace wf
