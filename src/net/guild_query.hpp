#pragma once
// ---------------------------------------------------------------------------
// Guild query codec for vanilla 1.12.1 (build 5875). When the client sees a
// guild id it can't name (a roster line, a chat tag, a nameplate <Guild>), it
// asks for the guild's public identity and caches the reply. This module
// reconstructs that exchange.
//
//   client -> CMSG_GUILD_QUERY          { u32 guildId }
//   server -> SMSG_GUILD_QUERY_RESPONSE { u32 guildId, cstr name,
//                                         cstr rankName[10],
//                                         u32 emblemStyle, u32 emblemColor,
//                                         u32 borderStyle, u32 borderColor,
//                                         u32 background }
//
// Opcode values (vanilla 1.12.1): CMSG_GUILD_QUERY = 0x054,
// SMSG_GUILD_QUERY_RESPONSE = 0x055.
//
// Vanilla-specific detail (verified vs mangos-zero Guild::Query): the response
// ALWAYS carries exactly ten rank-name slots regardless of how many ranks the
// guild actually has -- unused slots are written as an empty cstring (a lone
// NUL byte), not omitted. Rank names and the guild name are BARE cstrings (no
// length prefix). The guild id is a plain u32 (not an ObjectGuid). All five
// emblem/tabard fields are u32 -- a vanilla trait; the client's tabard shader
// reads them as full dwords.
//
// Pure over byte_reader/byte_writer -- no sockets, no opcode enums -- so the
// exchange round-trips deterministically (tests/test_guild_query.cpp).
// Cross-checked vs the GPL mangos-zero vanilla source used as a fact reference;
// our own code, no copy.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"   // writeCString / readCString

namespace wf {

// The client always renders ten rank slots for the guild control panel, so the
// server pads the response out to this fixed count (verified vs mangos-zero
// GUILD_RANKS_MAX_COUNT).
constexpr size_t GUILD_RANKS_MAX_COUNT = 10;

// ---- CMSG_GUILD_QUERY (client request) --------------------------------------
inline std::vector<uint8_t> encodeGuildQuery(uint32_t guildId) {
    ByteWriter w;
    w.u32(guildId);
    return w.data();
}
inline uint32_t decodeGuildQuery(ByteReader& r) {
    return r.u32();
}

// ---- SMSG_GUILD_QUERY_RESPONSE (server reply) -------------------------------
struct GuildQueryResponse {
    uint32_t    guildId = 0;
    std::string name;
    std::array<std::string, GUILD_RANKS_MAX_COUNT> rankNames{};  // always 10 on the wire
    uint32_t    emblemStyle = 0;   // tabard emblem symbol
    uint32_t    emblemColor = 0;   // emblem tint
    uint32_t    borderStyle = 0;   // tabard border symbol
    uint32_t    borderColor = 0;   // border tint
    uint32_t    background  = 0;   // background color
};

inline std::vector<uint8_t> encodeGuildQueryResponse(const GuildQueryResponse& g) {
    ByteWriter w;
    w.u32(g.guildId);
    writeCString(w, g.name);
    for (size_t i = 0; i < GUILD_RANKS_MAX_COUNT; ++i)
        writeCString(w, g.rankNames[i]);   // empty slot -> single NUL byte
    w.u32(g.emblemStyle);
    w.u32(g.emblemColor);
    w.u32(g.borderStyle);
    w.u32(g.borderColor);
    w.u32(g.background);
    return w.data();
}

inline GuildQueryResponse decodeGuildQueryResponse(ByteReader& r) {
    GuildQueryResponse g;
    g.guildId = r.u32();
    g.name    = readCString(r);
    for (size_t i = 0; i < GUILD_RANKS_MAX_COUNT; ++i)
        g.rankNames[i] = readCString(r);
    g.emblemStyle = r.u32();
    g.emblemColor = r.u32();
    g.borderStyle = r.u32();
    g.borderColor = r.u32();
    g.background  = r.u32();
    return g;
}

}  // namespace wf
