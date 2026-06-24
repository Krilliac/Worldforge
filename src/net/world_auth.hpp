#pragma once
// ---------------------------------------------------------------------------
// World-link (mangosd, port from the realm list) auth handshake for vanilla
// 1.12.1. This is the SECOND protocol; framing + the rolling header cipher live
// in src/worldproto.hpp. The handshake itself runs in PLAINTEXT (the cipher only
// engages for packets AFTER CMSG_AUTH_SESSION):
//
//   server -> SMSG_AUTH_CHALLENGE { u32 server_seed }
//   client -> CMSG_AUTH_SESSION   { build, server_id, username, client_seed,
//                                   digest[20], addon_info }
//   server -> SMSG_AUTH_RESPONSE  { u8 result, ... }
//
// The digest is the proof that the client holds the SRP6 session key K:
//   digest = SHA1( username | u32(0) | client_seed | server_seed | K )
// After verifying it, BOTH sides key WorldHeaderCrypt from K.
//
// All functions here are PURE (byte_writer/byte_reader + wf::sha1) -- no sockets
// -- so the digest can be checked against a fixed vector and the encode/decode
// round-tripped offline (tests/test_net.cpp).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "crypto.hpp"

namespace wf {
namespace net {

// SMSG_AUTH_CHALLENGE body == just the random server seed.
inline uint32_t decodeAuthChallenge(const std::vector<uint8_t>& body) {
    ByteReader r(body);
    return r.u32();
}
inline std::vector<uint8_t> encodeAuthChallenge(uint32_t serverSeed) {
    ByteWriter w;
    w.u32(serverSeed);
    return w.take();
}

// The CMSG_AUTH_SESSION digest. `username` is the raw (UPPERCASED) account-name
// bytes, `t` is a literal zero dword, and K is the 40-byte SRP6 session key fed
// in as raw bytes. Matches CMaNGOS WorldSocket::HandleAuthSession exactly.
inline std::array<uint8_t, 20> authSessionDigest(const std::string& username,
                                                 uint32_t clientSeed,
                                                 uint32_t serverSeed,
                                                 const std::vector<uint8_t>& K) {
    ByteWriter w;
    for (char c : username) w.u8(static_cast<uint8_t>(c));
    w.u32(0);                       // t
    w.u32(clientSeed);
    w.u32(serverSeed);
    w.bytes(K.data(), K.size());
    return sha1(w.data());
}

// CMSG_AUTH_SESSION body (the part after the 6-byte client header).
struct AuthSession {
    uint32_t              build      = 5875;
    uint32_t              serverId   = 0;       // a.k.a. unk2 / login-server-id
    std::string           username;            // UPPERCASED account name
    uint32_t              clientSeed = 0;
    std::array<uint8_t, 20> digest{};
    std::vector<uint8_t>  addonInfo;            // zlib addon blob; empty tolerated
};

inline std::vector<uint8_t> encodeAuthSession(const AuthSession& s) {
    ByteWriter w;
    w.u32(s.build);
    w.u32(s.serverId);
    for (char c : s.username) w.u8(static_cast<uint8_t>(c));
    w.u8(0);                        // nul-terminate username
    w.u32(s.clientSeed);
    w.bytes(s.digest.data(), s.digest.size());
    w.bytes(s.addonInfo.data(), s.addonInfo.size());
    return w.take();
}

// Build CMSG_AUTH_SESSION computing the digest from the session key.
inline std::vector<uint8_t> buildAuthSession(const std::string& account,
                                             uint32_t clientSeed,
                                             uint32_t serverSeed,
                                             const std::vector<uint8_t>& K,
                                             uint32_t build = 5875,
                                             const std::vector<uint8_t>& addonInfo = {}) {
    AuthSession s;
    s.build      = build;
    s.username   = account;
    s.clientSeed = clientSeed;
    s.digest     = authSessionDigest(account, clientSeed, serverSeed, K);
    s.addonInfo  = addonInfo;
    return encodeAuthSession(s);
}

inline AuthSession decodeAuthSession(const std::vector<uint8_t>& body) {
    AuthSession s;
    ByteReader r(body);
    s.build    = r.u32();
    s.serverId = r.u32();
    while (r.remaining() > 0) {
        uint8_t c = r.u8();
        if (c == 0) break;
        s.username += static_cast<char>(c);
    }
    s.clientSeed = r.u32();
    for (auto& b : s.digest) b = r.u8();
    s.addonInfo.assign(r.ptr(), r.ptr() + r.remaining());
    return s;
}

// SMSG_AUTH_RESPONSE. On OK vanilla appends billing fields; on queue a position.
enum class AuthResponse : uint8_t {
    OK         = 0x0C,
    WAIT_QUEUE = 0x1B,
};

struct AuthResponseMsg {
    AuthResponse result = AuthResponse::OK;
    uint32_t     queuePosition = 0;   // valid when result == WAIT_QUEUE
};

inline std::vector<uint8_t> encodeAuthResponse(const AuthResponseMsg& m) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(m.result));
    if (m.result == AuthResponse::OK) {
        w.u32(0);                   // billing time remaining
        w.u8(0);                    // billing flags
        w.u32(0);                   // billing time rested
    } else if (m.result == AuthResponse::WAIT_QUEUE) {
        w.u32(m.queuePosition);
    }
    return w.take();
}

inline AuthResponseMsg decodeAuthResponse(const std::vector<uint8_t>& body) {
    AuthResponseMsg m;
    ByteReader r(body);
    m.result = static_cast<AuthResponse>(r.u8());
    if (m.result == AuthResponse::WAIT_QUEUE && r.remaining() >= 4)
        m.queuePosition = r.u32();
    return m;
}

} // namespace net
} // namespace wf
