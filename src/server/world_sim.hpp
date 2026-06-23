#pragma once
// ---------------------------------------------------------------------------
// WorldSim: a tiny in-memory authoritative world the bridge mutates -- the
// server-agnostic analog of mangos's object store (Map / ObjectAccessor). The
// stub bridge server applies decoded EDITOR_* ops to this; in the real server,
// WorldForgeBridge calls Map::CreatureRelocation / SummonCreature / etc. instead.
// Pure logic, unit-tested -- the verifiable core of "mutate server state".
// ---------------------------------------------------------------------------
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "math.hpp"

namespace wf {

struct SimObject {
    uint64_t guid = 0;
    uint32_t entry = 0;
    uint32_t mapId = 0;
    Vec3     pos;
    float    orientation = 0.0f;
    std::vector<Vec3> waypoints;
};

// A log of the last applied client-FX (what the real server would broadcast).
struct FxLog {
    int      weatherCount = 0;   uint32_t weatherType = 0; float weatherGrade = 0.0f;
    int      soundCount   = 0;   uint32_t lastSound   = 0;
    int      cinematicCount = 0; uint32_t lastCinematic = 0;
    int      worldStateCount = 0;
    int      lightCount   = 0;   uint32_t lastOverrideLight = 0;
};

class WorldSim {
public:
    // Spawn a creature; returns the assigned GUID (UNIT high-guid space).
    uint64_t spawnCreature(uint32_t entry, uint32_t mapId, const Vec3& pos, float orientation);

    bool moveObject(uint64_t guid, const Vec3& pos, float orientation);
    bool despawn(uint64_t guid);
    bool setWaypoints(uint64_t guid, const std::vector<Vec3>& path);

    const SimObject* find(uint64_t guid) const;
    size_t aliveCount() const { return objects_.size(); }
    std::vector<uint64_t> guids() const;

    FxLog&       fx()       { return fx_; }
    const FxLog& fx() const { return fx_; }

private:
    std::unordered_map<uint64_t, SimObject> objects_;
    uint64_t nextGuid_ = 0xF130000000000001ull;   // UNIT high-guid base
    FxLog    fx_;
};

} // namespace wf
