#pragma once
// ---------------------------------------------------------------------------
// Editor RPC channel (WorldForge side). The wire format the editor host uses to
// drive a running mangos-zero: length+opcode framing in an EDITOR_* opcode
// range distinct from the game opcode stream, and NO header cipher (trusted
// local link). The server drains these and calls Map::CreatureRelocation /
// SummonCreature / ForcedDespawn / MotionMaster so its existing visibility
// system broadcasts to unmodified 1.12.1 clients -- see docs/EDITOR_RESEARCH.md
// part D. This header defines the op structs + encode/decode; the socket and
// the mangos-side handlers live outside this tree (server module).
//
// Frame: [uint16 size BE][uint32 opcode LE][payload].  `size` counts the opcode
// (4 bytes) + payload, matching the vanilla client-header convention so the
// framing code is shared with worldproto.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "image.hpp"     // Rgba
#include "math.hpp"
#include "debugdraw.hpp" // DebugDraw / DebugCategory (apply* helpers)

namespace wf {

// Editor opcodes. Kept well above the vanilla opcode space (which tops out in
// the 0x5xx range) so they can never collide with a game packet.
enum EditorOpcode : uint32_t {
    EDITOR_HELLO          = 0x4000,  // handshake / capability announce
    EDITOR_MOVE_OBJECT    = 0x4001,  // relocate a live object
    EDITOR_SPAWN_CREATURE = 0x4002,  // spawn a creature by template entry
    EDITOR_DESPAWN        = 0x4003,  // remove a live object
    EDITOR_SET_WAYPOINTS  = 0x4004,  // install a patrol path
    EDITOR_ACK            = 0x4005,  // server -> editor op result
    EDITOR_OVERRIDE_LIGHT = 0x4006,  // drive server-handled override-light (custom)

    // Atmosphere / World "client-FX" override ops (editor -> server). Each is a
    // scope-aware request; the server realises it into the matching clientfx
    // SMSG and broadcasts to the scope. See fxbridge.hpp and the live-override
    // plan. Kept in their own 0x402x block.
    EDITOR_FX_WEATHER     = 0x4020,
    EDITOR_FX_SOUND       = 0x4021,  // music or ambient sound
    EDITOR_FX_CINEMATIC   = 0x4022,
    EDITOR_FX_WORLDSTATE  = 0x4023,
    EDITOR_FX_SCREENMSG   = 0x4024,  // area-trigger / notification / server msg
    EDITOR_FX_TIMESPEED   = 0x4025,
    EDITOR_FX_ZONEATTACK  = 0x4026,

    // Debug-visualisation stream (server -> editor). Mirrors the mangoszero
    // `.debug vis ...` outputs (server PR #386) so the WorldForge viewport can
    // render the same cells / LoS / paths / collision data natively as a second
    // consumer, instead of (or alongside) the in-world GO markers.
    EDITOR_DEBUG_CLEAR    = 0x4010,  // drop all debug primitives
    EDITOR_DEBUG_MARKER   = 0x4011,  // a point + captured value + label
    EDITOR_DEBUG_LINE     = 0x4012,  // a ray/segment with an optional hit point
    EDITOR_DEBUG_PATH     = 0x4013,  // a navmesh path (good or bad)
    EDITOR_DEBUG_VOLUME   = 0x4014,  // a cell/trigger box or sphere

    // Live runtime-data stream (server -> editor). The server broadcasts the
    // authoritative state of every visible object each sim tick; the engine
    // mirrors it (WorldView) so the inspector + viewport show NPCs and players
    // actually moving. ENTITY_REMOVE retires an object that left the world.
    EDITOR_ENTITY_STATE   = 0x4030,  // one object's live state this tick
    EDITOR_ENTITY_REMOVE  = 0x4031,  // an object despawned / left visibility
    EDITOR_SERVER_STATE   = 0x4032,  // periodic server runtime status (FX + clock)
};

// The server's DV_* marker types (stable wire values; map to DebugCategory).
enum class DebugVisType : uint8_t {
    Generic = 0,
    Cell,        // DV_CELL
    LosOk,       // DV_LOS_OK
    LosBlock,    // DV_LOS_BLOCK
    Path,        // DV_PATH
    PathBad,     // DV_PATH_BAD
    Collision,   // DV_COLLISION
    HitPoint,    // DV_HITPOINT
    Height,      // DV_HEIGHT
};

// Map a server DV_* type onto the renderer's toggleable layer.
DebugCategory categoryFor(DebugVisType t);

// Live state of one world object (server -> editor, streamed per sim tick).
// `kind` matches WorldSim's EntityKind wire values (0 creature, 1 player,
// 2 gameobject). `moving` is the server's "is patrolling" flag this tick.
struct EntityState {
    uint64_t    guid  = 0;
    uint8_t     kind  = 0;
    uint32_t    entry = 0;
    uint32_t    mapId = 0;
    Vec3        pos;
    float       orientation = 0.0f;
    bool        moving = false;
    float       speed  = 0.0f;
    float       boundingRadius = 0.0f;   // selectable/model radius (0 = unknown)
    std::string name;
};
struct EntityRemove { uint64_t guid = 0; };

// Periodic server runtime status (server -> editor): the world clock, how many
// objects are alive, and the last-applied client-FX state (the FxLog the server
// would broadcast). Lets the inspector show "what the server is doing right now"
// alongside the per-entity stream.
struct ServerStatus {
    uint64_t simTimeMs   = 0;
    uint32_t entityCount = 0;
    uint32_t weatherType = 0;   float weatherGrade = 0.0f;
    uint32_t lastSound        = 0;
    uint32_t lastCinematic    = 0;
    uint32_t lastOverrideLight = 0;
    uint32_t weatherCount = 0, soundCount = 0, cinematicCount = 0,
             worldStateCount = 0, lightCount = 0;
};

// ---- debug-stream messages (mirror the server's captured data) ----
struct DebugMarker { DebugVisType type = DebugVisType::Generic; Vec3 pos; Rgba color; float value = 0.0f; std::string label; };
struct DebugLine   { DebugVisType type = DebugVisType::LosOk;    Vec3 from; Vec3 to; Rgba color; bool hasHit = false; Vec3 hit; };
struct DebugPath   { uint64_t guid = 0; bool bad = false; std::vector<Vec3> points; Rgba color; };
struct DebugVolume { uint8_t kind = 0; /*0 box, 1 sphere*/ Vec3 center; Vec3 half; float radius = 0.0f; Rgba color; DebugVisType type = DebugVisType::Cell; };

// Every mutating op carries a client-side opId for ack/undo correlation.
struct MoveObject   { uint64_t guid = 0; Vec3 pos; float orientation = 0.0f; uint32_t opId = 0; };
struct SpawnCreature{ uint32_t entry = 0; uint32_t mapId = 0; Vec3 pos; float orientation = 0.0f; uint32_t opId = 0; };
struct Despawn      { uint64_t guid = 0; uint32_t opId = 0; };
struct SetWaypoints { uint64_t guid = 0; std::vector<Vec3> path; uint32_t opId = 0; };
struct Ack          { uint32_t opId = 0; uint8_t status = 0; }; // status: 0 ok, !=0 error

// Recipient scope for client-FX override ops (matches the server's
// live-override-commands plan: who the resulting effect targets).
enum class FxScope : uint8_t { Self = 0, Target = 1, Zone = 2, Server = 3 };

// Who a scope-aware FX op targets. guid is used for Self/Target; zoneId for Zone.
struct FxTarget {
    FxScope  scope  = FxScope::Zone;
    uint64_t guid   = 0;
    uint32_t zoneId = 0;
};

// Editor -> server override-light request. A custom RPC: the server applies the
// light (id + fade) to the chosen scope and translates it to whatever a 1.12.1
// client can see. Decoupled from the raw 0x411 wire layout so the editor can
// carry scope/target the SMSG body has no room for.
struct OverrideLight {
    uint32_t overrideLightId = 0;
    uint32_t fadeInMs        = 0;
    FxScope  scope           = FxScope::Zone;
    uint64_t targetGuid      = 0;   // Self/Target scope (0 otherwise)
    uint32_t zoneId          = 0;   // Zone scope
    uint32_t opId            = 0;
};

// A decoded frame: the opcode plus the raw payload bytes (decode the matching
// struct with the decode* helpers below).
struct EditorFrame { uint32_t opcode = 0; std::vector<uint8_t> payload; };

// ---- framing ----
std::vector<uint8_t> frame(uint32_t opcode, const std::vector<uint8_t>& payload);
// Parse one frame from the front of `buf`. On success fills `out` and sets
// `consumed` to the total bytes used; returns false if `buf` is a short read.
bool readFrame(const std::vector<uint8_t>& buf, EditorFrame& out, size_t& consumed);

// ---- encode (editor -> server) ----
std::vector<uint8_t> encode(const MoveObject&);
std::vector<uint8_t> encode(const SpawnCreature&);
std::vector<uint8_t> encode(const Despawn&);
std::vector<uint8_t> encode(const SetWaypoints&);
std::vector<uint8_t> encode(const Ack&);
std::vector<uint8_t> encode(const OverrideLight&);

// ---- decode (from a frame's payload) ----
MoveObject    decodeMoveObject(const std::vector<uint8_t>& payload);
SpawnCreature decodeSpawnCreature(const std::vector<uint8_t>& payload);
Despawn       decodeDespawn(const std::vector<uint8_t>& payload);
SetWaypoints  decodeSetWaypoints(const std::vector<uint8_t>& payload);
Ack           decodeAck(const std::vector<uint8_t>& payload);
OverrideLight decodeOverrideLight(const std::vector<uint8_t>& payload);

// ---- debug stream encode / decode ----
std::vector<uint8_t> encode(const DebugMarker&);
std::vector<uint8_t> encode(const DebugLine&);
std::vector<uint8_t> encode(const DebugPath&);
std::vector<uint8_t> encode(const DebugVolume&);
DebugMarker decodeDebugMarker(const std::vector<uint8_t>& payload);
DebugLine   decodeDebugLine(const std::vector<uint8_t>& payload);
DebugPath   decodeDebugPath(const std::vector<uint8_t>& payload);
DebugVolume decodeDebugVolume(const std::vector<uint8_t>& payload);

// ---- live entity stream encode / decode ----
std::vector<uint8_t> encode(const EntityState&);
std::vector<uint8_t> encode(const EntityRemove&);
std::vector<uint8_t> encode(const ServerStatus&);
EntityState  decodeEntityState(const std::vector<uint8_t>& payload);
EntityRemove decodeEntityRemove(const std::vector<uint8_t>& payload);
ServerStatus decodeServerStatus(const std::vector<uint8_t>& payload);

// ---- apply a decoded debug message into a renderable DebugDraw ----
void apply(DebugDraw& dd, const DebugMarker&);
void apply(DebugDraw& dd, const DebugLine&);
void apply(DebugDraw& dd, const DebugPath&);
void apply(DebugDraw& dd, const DebugVolume&);

} // namespace wf
