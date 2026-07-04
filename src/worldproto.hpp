#pragma once
// ---------------------------------------------------------------------------
// Vanilla 1.12.1 world protocol bits: the rolling header cipher (seeded from
// the SRP6 session key -- NOT ARC4, which is 2.x+), packet framing, and a small
// set of opcodes. The cipher only ever touches the packet *header*; the body is
// plaintext.
//
// Server header: uint16 size (big endian) + uint16 opcode (little endian) = 4B.
// Client header: uint16 size (big endian) + uint32 opcode (little endian) = 6B.
// `size` counts the opcode bytes plus the payload, but not the size field.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace wf {

// A subset of vanilla opcodes (the logon path plus the movement opcode that
// matters for the live-server editor bridge).
enum Opcode : uint32_t {
    SMSG_AUTH_CHALLENGE = 0x1EC,
    CMSG_AUTH_SESSION   = 0x1ED,
    SMSG_AUTH_RESPONSE  = 0x1EE,
    CMSG_PING           = 0x1DC,
    SMSG_PONG           = 0x1DD,
    CMSG_CHAR_ENUM      = 0x037,
    SMSG_CHAR_ENUM      = 0x03B,
    CMSG_PLAYER_LOGIN   = 0x03D,
    SMSG_UPDATE_OBJECT  = 0x0A9,
    SMSG_MONSTER_MOVE   = 0x0DD,

    // Player movement (MSG_* = same opcode both ways: client sends its own move,
    // server relays other players'). Vanilla 1.12.1 values, cross-checked vs
    // cmangos / mangos-zero Opcodes.h. Each body is a MovementInfo; the server
    // relay form prepends the mover's packed guid (see net/movement.hpp).
    MSG_MOVE_START_FORWARD       = 0x0B5,
    MSG_MOVE_START_BACKWARD      = 0x0B6,
    MSG_MOVE_STOP                = 0x0B7,
    MSG_MOVE_START_STRAFE_LEFT   = 0x0B8,
    MSG_MOVE_START_STRAFE_RIGHT  = 0x0B9,
    MSG_MOVE_STOP_STRAFE         = 0x0BA,
    MSG_MOVE_JUMP                = 0x0BB,
    MSG_MOVE_START_TURN_LEFT     = 0x0BC,
    MSG_MOVE_START_TURN_RIGHT    = 0x0BD,
    MSG_MOVE_STOP_TURN           = 0x0BE,
    MSG_MOVE_START_PITCH_UP      = 0x0BF,
    MSG_MOVE_START_PITCH_DOWN    = 0x0C0,
    MSG_MOVE_STOP_PITCH          = 0x0C1,
    MSG_MOVE_SET_RUN_MODE        = 0x0C2,
    MSG_MOVE_SET_WALK_MODE       = 0x0C3,
    MSG_MOVE_FALL_LAND           = 0x0C9,
    MSG_MOVE_SET_FACING          = 0x0DA,
    MSG_MOVE_SET_PITCH           = 0x0DB,
    MSG_MOVE_HEARTBEAT           = 0x0EE,

    // "The client already knows how to render it" server opcodes -- spectacle /
    // atmosphere / HUD that need only an id or a short body. Values verified for
    // build 5875 against mangos-zero and cmangos Opcodes.h. Builders in
    // clientfx.hpp. NB: SMSG_OVERRIDE_LIGHT (0x411) is deliberately absent -- it
    // is a TBC+ opcode with no handler in a 1.12.1 client.
    SMSG_LOGIN_SETTIMESPEED     = 0x042,
    SMSG_GAMEOBJECT_CUSTOM_ANIM = 0x0B3,
    SMSG_TRIGGER_CINEMATIC      = 0x0FA,
    SMSG_NOTIFICATION           = 0x1CB,
    MSG_MINIMAP_PING            = 0x1D5,
    SMSG_PLAY_SPELL_VISUAL      = 0x1F3,
    SMSG_PLAY_SPELL_IMPACT      = 0x1F7,
    SMSG_ZONE_UNDER_ATTACK      = 0x254,
    SMSG_PLAY_MUSIC             = 0x277,
    SMSG_PLAY_OBJECT_SOUND      = 0x278,
    SMSG_SERVER_MESSAGE         = 0x291,
    SMSG_AREA_TRIGGER_MESSAGE   = 0x2B8,
    SMSG_INIT_WORLD_STATES      = 0x2C2,
    SMSG_UPDATE_WORLD_STATE     = 0x2C3,
    SMSG_PLAY_SOUND             = 0x2D2,
    SMSG_WEATHER                = 0x2F4,

    // More underused/debug spectacle opcodes (verified vs cmangos vanilla).
    SMSG_EMOTE                   = 0x103,  // u32 emoteId; u64 guid
    SMSG_AI_REACTION             = 0x13C,  // u64 guid; u32 reaction
    SMSG_EXPLORATION_EXPERIENCE  = 0x1F8,  // u32 areaId; u32 xp
    SMSG_GAMEOBJECT_DESPAWN_ANIM = 0x215,  // u64 guid
    SMSG_STANDSTATE_UPDATE       = 0x29D,  // u8 state

    // Custom / trusted-link opcode. 0x411 is NOT a 1.12.1 retail-client render
    // path (it is TBC+), but it IS present in mangos-zero's opcode table, so the
    // WorldForge<->server bridge can use it as a server-handled custom message:
    // WorldForge frames it, the server's handler applies the lighting change
    // (and translates it to whatever a vanilla client can actually see). See
    // docs/SERVER_OPCODES.md and clientfx buildOverrideLight.
    SMSG_OVERRIDE_LIGHT         = 0x411,
};

// Rolling add/xor header cipher. Send and receive directions keep independent
// running state, exactly like a real client/server endpoint.
class WorldHeaderCrypt {
public:
    explicit WorldHeaderCrypt(std::vector<uint8_t> sessionKey)
        : key_(std::move(sessionKey)) {
        if (key_.empty()) throw std::runtime_error("empty session key");
    }

    void encryptSend(uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            uint8_t e = static_cast<uint8_t>((data[i] ^ key_[sendIdx_ % key_.size()]) + sendPrev_);
            sendPrev_ = e;
            ++sendIdx_;
            data[i] = e;
        }
    }

    void decryptRecv(uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            uint8_t enc = data[i];
            uint8_t d = static_cast<uint8_t>((enc - recvPrev_) ^ key_[recvIdx_ % key_.size()]);
            recvPrev_ = enc;
            ++recvIdx_;
            data[i] = d;
        }
    }

private:
    std::vector<uint8_t> key_;
    size_t  sendIdx_ = 0, recvIdx_ = 0;
    uint8_t sendPrev_ = 0, recvPrev_ = 0;
};

// ---- framing ----
struct ServerHeader { uint16_t opcode; uint32_t payloadLen; };
struct ClientHeader { uint32_t opcode; uint32_t payloadLen; };

inline std::vector<uint8_t> writeServerHeader(uint16_t opcode, uint32_t payloadLen) {
    uint16_t size = static_cast<uint16_t>(payloadLen + 2);
    return { static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size & 0xFF),
             static_cast<uint8_t>(opcode & 0xFF), static_cast<uint8_t>(opcode >> 8) };
}

inline ServerHeader readServerHeader(const uint8_t* h) {
    uint16_t size   = static_cast<uint16_t>((h[0] << 8) | h[1]);
    uint16_t opcode = static_cast<uint16_t>(h[2] | (h[3] << 8));
    return { opcode, static_cast<uint32_t>(size - 2) };
}

inline std::vector<uint8_t> writeClientHeader(uint32_t opcode, uint32_t payloadLen) {
    uint16_t size = static_cast<uint16_t>(payloadLen + 4);
    return { static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size & 0xFF),
             static_cast<uint8_t>(opcode & 0xFF), static_cast<uint8_t>((opcode >> 8) & 0xFF),
             static_cast<uint8_t>((opcode >> 16) & 0xFF), static_cast<uint8_t>((opcode >> 24) & 0xFF) };
}

inline ClientHeader readClientHeader(const uint8_t* h) {
    uint16_t size = static_cast<uint16_t>((h[0] << 8) | h[1]);
    uint32_t opcode = static_cast<uint32_t>(h[2]) | (static_cast<uint32_t>(h[3]) << 8) |
                      (static_cast<uint32_t>(h[4]) << 16) | (static_cast<uint32_t>(h[5]) << 24);
    return { opcode, static_cast<uint32_t>(size - 4) };
}

} // namespace wf
