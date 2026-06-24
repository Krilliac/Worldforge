#pragma once
// ---------------------------------------------------------------------------
// Enter-world pump for vanilla 1.12.1 (build 5875): the opcodes the client
// drives AFTER the world auth handshake (src/net/world_auth.hpp) to go from a
// freshly-authenticated world connection to "standing in the world".
//
//   client -> CMSG_CHAR_ENUM        (empty body)
//   server -> SMSG_CHAR_ENUM        { u8 count; per-char block... }
//   client -> CMSG_PLAYER_LOGIN     { u64 guid }
//   server -> SMSG_LOGIN_VERIFY_WORLD { u32 map; f32 x,y,z,o }   (now in-world)
//   ...     CMSG_PING <-> SMSG_PONG keep-alive (no SMSG_TIME_SYNC_REQ in 1.12)
//
// Everything here is a PURE encode/decode function over byte_writer/byte_reader
// (no sockets), so the whole entry handshake round-trips offline. Byte layouts
// cross-checked against CMaNGOS HandleCharEnumOpcode / HandlePlayerLoginOpcode /
// Player::SendInitialPacketsBeforeAddToMap and the GTKer wire docs; see
// docs/research/3-online-login-world-entry.md section 2.5.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "server/world_sim.hpp"

namespace wf {
namespace net {

// ---- CMSG_CHAR_ENUM ---------------------------------------------------------
// Empty body; the opcode alone carries the request. Provided for symmetry.
inline std::vector<uint8_t> encodeCharEnumRequest() {
    return {};
}

// One equipped item slot in the character screen preview (vanilla layout).
struct CharEnumItem {
    uint32_t displayId    = 0;
    uint8_t  inventoryType = 0;
    uint32_t enchantAuraId = 0;
};

// A single character row in SMSG_CHAR_ENUM. Only the fields WorldForge needs to
// pick the avatar to log in and place it are surfaced as named members; the rest
// are round-tripped verbatim so a stub server and the client agree byte-for-byte.
struct CharEnumEntry {
    uint64_t    guid   = 0;
    std::string name;
    uint8_t     race   = 0;
    uint8_t     clazz  = 0;
    uint8_t     gender = 0;
    uint8_t     skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
    uint8_t     level  = 1;
    uint32_t    zone   = 0;
    uint32_t    mapId  = 0;
    float       x = 0, y = 0, z = 0;
    uint32_t    guildId   = 0;
    uint32_t    charFlags = 0;
    uint8_t     firstLogin = 0;
    uint32_t    petDisplayId = 0, petLevel = 0, petFamily = 0;
    std::array<CharEnumItem, 19> equipment{};   // vanilla: 19 equipment slots
};

inline void encodeCharEnumEntry(ByteWriter& w, const CharEnumEntry& c) {
    w.u64(c.guid);
    for (char ch : c.name) w.u8(static_cast<uint8_t>(ch));
    w.u8(0);                                     // nul-terminate name
    w.u8(c.race);
    w.u8(c.clazz);
    w.u8(c.gender);
    w.u8(c.skin);
    w.u8(c.face);
    w.u8(c.hairStyle);
    w.u8(c.hairColor);
    w.u8(c.facialHair);
    w.u8(c.level);
    w.u32(c.zone);
    w.u32(c.mapId);
    w.f32(c.x);
    w.f32(c.y);
    w.f32(c.z);
    w.u32(c.guildId);
    w.u32(c.charFlags);
    w.u8(c.firstLogin);
    w.u32(c.petDisplayId);
    w.u32(c.petLevel);
    w.u32(c.petFamily);
    for (const CharEnumItem& it : c.equipment) {
        w.u32(it.displayId);
        w.u8(it.inventoryType);
        w.u32(it.enchantAuraId);
    }
}

inline std::vector<uint8_t> encodeCharEnumReply(const std::vector<CharEnumEntry>& chars) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(chars.size()));
    for (const CharEnumEntry& c : chars) encodeCharEnumEntry(w, c);
    return w.take();
}

inline CharEnumEntry decodeCharEnumEntry(ByteReader& r) {
    CharEnumEntry c;
    c.guid = r.u64();
    while (r.remaining() > 0) {
        uint8_t ch = r.u8();
        if (ch == 0) break;
        c.name += static_cast<char>(ch);
    }
    c.race   = r.u8();
    c.clazz  = r.u8();
    c.gender = r.u8();
    c.skin = r.u8(); c.face = r.u8(); c.hairStyle = r.u8();
    c.hairColor = r.u8(); c.facialHair = r.u8();
    c.level = r.u8();
    c.zone  = r.u32();
    c.mapId = r.u32();
    c.x = r.f32(); c.y = r.f32(); c.z = r.f32();
    c.guildId   = r.u32();
    c.charFlags = r.u32();
    c.firstLogin = r.u8();
    c.petDisplayId = r.u32();
    c.petLevel     = r.u32();
    c.petFamily    = r.u32();
    for (CharEnumItem& it : c.equipment) {
        it.displayId     = r.u32();
        it.inventoryType = r.u8();
        it.enchantAuraId = r.u32();
    }
    return c;
}

inline std::vector<CharEnumEntry> decodeCharEnumReply(const std::vector<uint8_t>& body) {
    std::vector<CharEnumEntry> out;
    ByteReader r(body);
    if (r.remaining() == 0) return out;
    uint8_t count = r.u8();
    out.reserve(count);
    for (uint8_t i = 0; i < count; ++i) out.push_back(decodeCharEnumEntry(r));
    return out;
}

// ---- CMSG_PLAYER_LOGIN ------------------------------------------------------
inline std::vector<uint8_t> encodePlayerLogin(uint64_t guid) {
    ByteWriter w;
    w.u64(guid);
    return w.take();
}
inline uint64_t decodePlayerLogin(const std::vector<uint8_t>& body) {
    ByteReader r(body);
    return r.u64();
}

// ---- SMSG_LOGIN_VERIFY_WORLD ------------------------------------------------
// The packet that means "you are now in the world at this spot".
struct LoginVerifyWorld {
    uint32_t mapId = 0;
    float    x = 0, y = 0, z = 0, orientation = 0;
};

inline std::vector<uint8_t> encodeLoginVerifyWorld(const LoginVerifyWorld& v) {
    ByteWriter w;
    w.u32(v.mapId);
    w.f32(v.x);
    w.f32(v.y);
    w.f32(v.z);
    w.f32(v.orientation);
    return w.take();
}
inline LoginVerifyWorld decodeLoginVerifyWorld(const std::vector<uint8_t>& body) {
    LoginVerifyWorld v;
    ByteReader r(body);
    v.mapId = r.u32();
    v.x = r.f32(); v.y = r.f32(); v.z = r.f32(); v.orientation = r.f32();
    return v;
}

// ---- CMSG_PING / SMSG_PONG keep-alive --------------------------------------
// The server drops a client that stops pinging. Vanilla 1.12 has NO
// SMSG_TIME_SYNC_REQ -- this ping/pong is the only keep-alive.
inline std::vector<uint8_t> encodePing(uint32_t pingId, uint32_t latencyMs) {
    ByteWriter w;
    w.u32(pingId);
    w.u32(latencyMs);
    return w.take();
}
struct Ping { uint32_t pingId = 0; uint32_t latencyMs = 0; };
inline Ping decodePing(const std::vector<uint8_t>& body) {
    Ping p;
    ByteReader r(body);
    p.pingId = r.u32();
    if (r.remaining() >= 4) p.latencyMs = r.u32();
    return p;
}
inline std::vector<uint8_t> encodePong(uint32_t pingId) {
    ByteWriter w;
    w.u32(pingId);
    return w.take();
}
inline uint32_t decodePong(const std::vector<uint8_t>& body) {
    ByteReader r(body);
    return r.u32();
}

// Convenience: turn a decoded CHAR_ENUM row into the SimObject the renderer and
// world_view already understand (a Player avatar at the character's last spot).
inline SimObject charEnumToSimObject(const CharEnumEntry& c) {
    SimObject o;
    o.guid  = c.guid;
    o.entry = 0;                 // players have no creature entry
    o.mapId = c.mapId;
    o.kind  = EntityKind::Player;
    o.name  = c.name;
    o.pos   = { c.x, c.y, c.z };
    o.orientation = 0.0f;
    return o;
}

} // namespace net
} // namespace wf
