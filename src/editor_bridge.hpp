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
#include <vector>

#include "math.hpp"

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
};

// Every mutating op carries a client-side opId for ack/undo correlation.
struct MoveObject   { uint64_t guid = 0; Vec3 pos; float orientation = 0.0f; uint32_t opId = 0; };
struct SpawnCreature{ uint32_t entry = 0; uint32_t mapId = 0; Vec3 pos; float orientation = 0.0f; uint32_t opId = 0; };
struct Despawn      { uint64_t guid = 0; uint32_t opId = 0; };
struct SetWaypoints { uint64_t guid = 0; std::vector<Vec3> path; uint32_t opId = 0; };
struct Ack          { uint32_t opId = 0; uint8_t status = 0; }; // status: 0 ok, !=0 error

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

// ---- decode (from a frame's payload) ----
MoveObject    decodeMoveObject(const std::vector<uint8_t>& payload);
SpawnCreature decodeSpawnCreature(const std::vector<uint8_t>& payload);
Despawn       decodeDespawn(const std::vector<uint8_t>& payload);
SetWaypoints  decodeSetWaypoints(const std::vector<uint8_t>& payload);
Ack           decodeAck(const std::vector<uint8_t>& payload);

} // namespace wf
