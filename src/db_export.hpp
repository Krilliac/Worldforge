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

} // namespace wf
