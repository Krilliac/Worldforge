#pragma once
// ---------------------------------------------------------------------------
// Entity query codecs for vanilla 1.12.1 (build 5875) -- the client-side cache
// fill (the alpha's DBClient "DBCache<T>" for server-pushed records). When the
// client sees a GUID it can't name (a player in chat, a nameplate), it sends a
// query and caches the response. This module reconstructs the *name* query
// (players); creature/gameobject queries share the shape and can follow.
//
//   client -> CMSG_NAME_QUERY          { u64 guid }
//   server -> SMSG_NAME_QUERY_RESPONSE { u64 guid, cstr name, u8 0 (realm),
//                                        u32 race, u32 gender, u32 class }
//
// Vanilla-specific detail (verified vs mangos-zero QueryHandler): race / gender
// / class are **u32** here -- later expansions narrowed them to u8. The guid is
// a raw u64 (not packed) in both directions.
//
// Pure over byte_reader/byte_writer -- no sockets -- so the exchange round-trips
// deterministically (tests/test_query.cpp). Cross-checked vs the GPL mangos-zero
// vanilla source used as a fact reference; no code copied.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "worldproto.hpp"   // CMSG_NAME_QUERY / SMSG_NAME_QUERY_RESPONSE

namespace wf {

// ---- NUL-terminated string helpers (WoW "CString") --------------------------
inline void writeCString(ByteWriter& w, const std::string& s) {
    for (char c : s) w.u8(static_cast<uint8_t>(c));
    w.u8(0);
}
inline std::string readCString(ByteReader& r) {
    std::string s;
    while (r.remaining() > 0) {
        uint8_t c = r.u8();
        if (c == 0) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

// ---- CMSG_NAME_QUERY (client request) ---------------------------------------
inline std::vector<uint8_t> encodeNameQuery(uint64_t guid) {
    ByteWriter w;
    w.u64(guid);
    return w.data();
}
inline uint64_t decodeNameQuery(ByteReader& r) {
    return r.u64();
}

// ---- SMSG_NAME_QUERY_RESPONSE (server reply) --------------------------------
struct NameQueryResponse {
    uint64_t    guid   = 0;
    std::string name;
    uint32_t    race   = 0;   // ChrRaces.dbc id
    uint32_t    gender = 0;   // 0 = male, 1 = female
    uint32_t    cls    = 0;   // ChrClasses.dbc id
};

inline std::vector<uint8_t> encodeNameQueryResponse(const NameQueryResponse& q) {
    ByteWriter w;
    w.u64(q.guid);
    writeCString(w, q.name);
    w.u8(0);                    // realm name: empty cstring (cross-realm BG, unused)
    w.u32(q.race);
    w.u32(q.gender);
    w.u32(q.cls);
    return w.data();
}

inline NameQueryResponse decodeNameQueryResponse(ByteReader& r) {
    NameQueryResponse q;
    q.guid   = r.u64();
    q.name   = readCString(r);
    (void)readCString(r);       // realm name (empty in vanilla)
    q.race   = r.u32();
    q.gender = r.u32();
    q.cls    = r.u32();
    return q;
}

}  // namespace wf
