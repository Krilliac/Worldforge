#pragma once
// ---------------------------------------------------------------------------
// WorldSim: a tiny in-memory authoritative world the bridge mutates -- the
// server-agnostic analog of mangos's object store (Map / ObjectAccessor). The
// stub bridge server applies decoded EDITOR_* ops to this; in the real server,
// WorldForgeBridge calls Map::CreatureRelocation / SummonCreature / etc. instead.
// Pure logic, unit-tested -- the verifiable core of "mutate server state".
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "math.hpp"

namespace wf {

struct EditorFrame;   // editor_bridge.hpp
struct Ack;

// What an object is, so the engine can pick an icon/model + colour. Wire values
// are stable (streamed in EntityState); mirrors the high-guid families that
// matter to the editor.
enum class EntityKind : uint8_t { Creature = 0, Player = 1, GameObject = 2 };

struct SimObject {
    uint64_t    guid  = 0;
    uint32_t    entry = 0;
    uint32_t    mapId = 0;
    EntityKind  kind  = EntityKind::Creature;
    std::string name;                       // optional display name
    Vec3        pos;
    float       orientation = 0.0f;
    float       speed   = 5.0f;             // yards/sec while patrolling
    float       radius  = 1.5f;             // selectable/model bounding radius
    Vec3        boundsMin;                   // model-local box (degenerate = none)
    Vec3        boundsMax;
    bool        moving  = false;            // advanced by the last tick
    std::vector<Vec3> waypoints;
    size_t      wpIndex = 0;                // current target waypoint
};

// One EDITOR_RELOAD_GRID the sim has honoured. The real server force-unloads
// the grid so its next activation re-reads .map/.vmtile/.mmtile from disk (the
// derived-file hot-swap); the sim just logs it so tests can assert the loop.
struct GridReload { uint32_t mapId = 0; int32_t gx = 0; int32_t gy = 0; };

// One EDITOR_SQL_APPLY the sim has honoured. The real server runs the SQL on
// the world DB then executes the ".reload" chat command; the sim logs both.
struct SqlRecord { std::string sql; std::string reloadCommand; };

// A temporary debug marker from EDITOR_MARK_POINTS. The real server realises
// each as a VISUAL_WAYPOINT creature with a timed despawn; the sim stores the
// point and expires it against the sim clock on tick().
struct SimMarker { Vec3 pos; uint64_t expireAtMs = 0; };

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
    // Spawn a player avatar (PLAYER high-guid space) -- so connected players show
    // up in the inspector / viewport alongside NPCs.
    uint64_t spawnPlayer(uint32_t mapId, const Vec3& pos, float orientation,
                         const std::string& name = {});

    bool moveObject(uint64_t guid, const Vec3& pos, float orientation);
    bool despawn(uint64_t guid);
    bool setWaypoints(uint64_t guid, const std::vector<Vec3>& path);
    bool setSpeed(uint64_t guid, float speed);
    bool setBounds(uint64_t guid, float radius);   // selectable/model radius
    bool setModelBox(uint64_t guid, const Vec3& localMin, const Vec3& localMax);

    // Advance the world by `dt` seconds: every object with a patrol path walks
    // toward its next waypoint at its speed, looping, facing its direction of
    // travel. Returns the number of objects that moved this tick.
    size_t tick(float dt);
    uint64_t simTimeMs() const { return simTimeMs_; }

    const SimObject* find(uint64_t guid) const;
    size_t aliveCount() const { return objects_.size(); }
    std::vector<uint64_t> guids() const;
    // A stable, GUID-sorted snapshot of every object (for streaming/inspecting).
    std::vector<SimObject> snapshot() const;

    FxLog&       fx()       { return fx_; }
    const FxLog& fx() const { return fx_; }

    // --- bridge ops beyond the object store (logged so the e2e tests can
    //     assert the full editor -> socket -> server -> ack loop) ---
    void reloadGrid(uint32_t mapId, int32_t gx, int32_t gy);
    void markPoints(const std::vector<Vec3>& points, uint32_t ttlMs);
    void applySql(const std::string& sql, const std::string& reloadCommand);

    // Decode + apply one editor frame if it is an op this sim owns
    // (RELOAD_GRID / MARK_POINTS / SQL_APPLY). Returns true when handled,
    // filling `ack` (the op's opId; status 0 ok, 1 malformed payload). Any
    // other opcode returns false so the caller dispatches it itself.
    bool handleEditorFrame(const EditorFrame& frame, Ack& ack);

    const std::vector<GridReload>& reloadedGrids() const { return reloadedGrids_; }
    const std::vector<SqlRecord>&  appliedSql()    const { return appliedSql_; }
    const std::vector<SimMarker>&  markers()       const { return markers_; }

private:
    std::unordered_map<uint64_t, SimObject> objects_;
    uint64_t nextGuid_       = 0xF130000000000001ull;   // UNIT high-guid base
    uint64_t nextPlayerGuid_ = 0x0000000000000001ull;   // PLAYER guids are low
    uint64_t simTimeMs_      = 0;
    FxLog    fx_;
    std::vector<GridReload> reloadedGrids_;
    std::vector<SqlRecord>  appliedSql_;
    std::vector<SimMarker>  markers_;
};

} // namespace wf
