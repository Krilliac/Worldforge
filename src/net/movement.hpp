#pragma once
// ---------------------------------------------------------------------------
// MovementInfo codec for vanilla 1.12.1 (build 5875): the shared movement block
// carried by every MSG_MOVE_* opcode and by the living-object update block. The
// update-object decoder (update_object.hpp) reads position out of this block
// inline; this is the standalone, round-trippable form used for live movement
// (client sends its own MovementInfo; the server relays another mover's as
// packed-guid + MovementInfo).
//
// Conditional sub-blocks appear only when their movement flag is set, in this
// exact order (mangos-zero / cmangos vanilla MovementInfo::Read):
//   flags:u32, time:u32, x,y,z,o:f32
//   [ONTRANSPORT]      transportGuid:u64, tx,ty,tz,to:f32, transportTime:u32
//   [SWIMMING]         pitch:f32
//   fallTime:f32
//   [JUMPING]          jumpVelocity, jumpSin, jumpCos, jumpXYSpeed:f32
//   [SPLINE_ELEVATION] splineElevation:f32
// NB vanilla reads the transport guid as a RAW u64 (packed-guid transport is a
// 2.x+ change); the mover guid in the server relay wrapper IS packed.
//
// Pure over byte_reader/byte_writer -- no sockets -- so every flag combination
// round-trips deterministically (tests/test_movement.cpp). Reuses the MoveFlag
// bits and readPackedGuid from update_object.hpp (no duplication).
// ---------------------------------------------------------------------------
#include <cstdint>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "math.hpp"                 // Vec3
#include "net/update_object.hpp"    // MoveFlag, readPackedGuid
#include "worldproto.hpp"           // MSG_MOVE_* opcodes

namespace wf {

// A decoded MovementInfo. Fields outside the active flag set stay at their
// defaults after a read, and are not emitted by a write.
struct MovementInfo {
    uint32_t flags = 0;
    uint32_t time  = 0;
    Vec3     pos;
    float    o = 0.0f;

    // ONTRANSPORT
    uint64_t transportGuid = 0;
    Vec3     transportPos;
    float    transportO    = 0.0f;
    uint32_t transportTime = 0;

    // SWIMMING
    float pitch = 0.0f;

    float fallTime = 0.0f;         // always present

    // JUMPING
    float jumpVelocity = 0.0f;     // vertical (z) speed
    float jumpSin      = 0.0f;
    float jumpCos      = 0.0f;
    float jumpXYSpeed  = 0.0f;

    // SPLINE_ELEVATION
    float splineElevation = 0.0f;

    bool has(net::MoveFlag f) const { return (flags & f) != 0; }
};

// Read a MovementInfo, consuming exactly the bytes its flag set implies.
inline MovementInfo readMovementInfo(ByteReader& r) {
    MovementInfo m;
    m.flags = r.u32();
    m.time  = r.u32();
    m.pos   = { r.f32(), r.f32(), r.f32() };
    m.o     = r.f32();

    if (m.flags & net::MOVEFLAG_ONTRANSPORT) {
        m.transportGuid = r.u64();                 // raw u64 (vanilla)
        m.transportPos  = { r.f32(), r.f32(), r.f32() };
        m.transportO    = r.f32();
        m.transportTime = r.u32();
    }
    if (m.flags & net::MOVEFLAG_SWIMMING) m.pitch = r.f32();
    m.fallTime = r.f32();
    if (m.flags & net::MOVEFLAG_JUMPING) {
        m.jumpVelocity = r.f32();
        m.jumpSin      = r.f32();
        m.jumpCos      = r.f32();
        m.jumpXYSpeed  = r.f32();
    }
    if (m.flags & net::MOVEFLAG_SPLINE_ELEVATION) m.splineElevation = r.f32();
    return m;
}

// Write a MovementInfo, emitting exactly the bytes readMovementInfo consumes.
inline void writeMovementInfo(ByteWriter& w, const MovementInfo& m) {
    w.u32(m.flags);
    w.u32(m.time);
    w.f32(m.pos.x); w.f32(m.pos.y); w.f32(m.pos.z);
    w.f32(m.o);

    if (m.flags & net::MOVEFLAG_ONTRANSPORT) {
        w.u64(m.transportGuid);
        w.f32(m.transportPos.x); w.f32(m.transportPos.y); w.f32(m.transportPos.z);
        w.f32(m.transportO);
        w.u32(m.transportTime);
    }
    if (m.flags & net::MOVEFLAG_SWIMMING) w.f32(m.pitch);
    w.f32(m.fallTime);
    if (m.flags & net::MOVEFLAG_JUMPING) {
        w.f32(m.jumpVelocity);
        w.f32(m.jumpSin);
        w.f32(m.jumpCos);
        w.f32(m.jumpXYSpeed);
    }
    if (m.flags & net::MOVEFLAG_SPLINE_ELEVATION) w.f32(m.splineElevation);
}

// ---- packed-guid write (mirror of update_object.hpp's readPackedGuid) --------
inline void writePackedGuid(ByteWriter& w, uint64_t guid) {
    uint8_t mask = 0, bytes[8];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t b = static_cast<uint8_t>(guid >> (8 * i));
        if (b) { mask |= static_cast<uint8_t>(1u << i); bytes[n++] = b; }
    }
    w.u8(mask);
    for (int i = 0; i < n; ++i) w.u8(bytes[i]);
}

// ---- server relay wrapper: a mover's packed guid + its MovementInfo ----------
// The body the server sends for another player's MSG_MOVE_* (the client uses the
// guid to find the unit and applies the MovementInfo). The client's own outgoing
// MSG_MOVE_* omits the guid (it is the sender), so use writeMovementInfo directly
// for that direction.
struct MoveRelay {
    uint64_t     guid = 0;
    MovementInfo info;
};

inline MoveRelay readMoveRelay(ByteReader& r) {
    MoveRelay m;
    m.guid = net::readPackedGuid(r);
    m.info = readMovementInfo(r);
    return m;
}

inline std::vector<uint8_t> writeMoveRelay(const MoveRelay& m) {
    ByteWriter w;
    writePackedGuid(w, m.guid);
    writeMovementInfo(w, m.info);
    return w.data();
}

// True if `op` is one of the MSG_MOVE_* movement opcodes (whose body is a
// MovementInfo / MoveRelay). Lets a dispatcher route the whole family together.
inline bool isMovementOpcode(uint32_t op) {
    switch (op) {
        case MSG_MOVE_START_FORWARD: case MSG_MOVE_START_BACKWARD:
        case MSG_MOVE_STOP:          case MSG_MOVE_START_STRAFE_LEFT:
        case MSG_MOVE_START_STRAFE_RIGHT: case MSG_MOVE_STOP_STRAFE:
        case MSG_MOVE_JUMP:          case MSG_MOVE_START_TURN_LEFT:
        case MSG_MOVE_START_TURN_RIGHT:   case MSG_MOVE_STOP_TURN:
        case MSG_MOVE_START_PITCH_UP:     case MSG_MOVE_START_PITCH_DOWN:
        case MSG_MOVE_STOP_PITCH:    case MSG_MOVE_SET_RUN_MODE:
        case MSG_MOVE_SET_WALK_MODE: case MSG_MOVE_FALL_LAND:
        case MSG_MOVE_SET_FACING:    case MSG_MOVE_SET_PITCH:
        case MSG_MOVE_HEARTBEAT:
            return true;
        default:
            return false;
    }
}

}  // namespace wf
