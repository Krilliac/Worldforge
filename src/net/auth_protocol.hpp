#pragma once
// ---------------------------------------------------------------------------
// Vanilla 1.12.1 (build 5875) logon / realmd "auth protocol" (port 3724).
//
// This is the FIRST of the two WoW TCP protocols. It is NOT header-encrypted:
// every message is a single leading opcode byte followed by a little-endian
// body. SRP6 runs here (the math lives in src/srp6.* + src/crypto.*); the realm
// list is returned at the end.
//
// Everything in this header is a PURE encode/decode function over
// byte_writer/byte_reader -- no sockets -- so the whole logon exchange can be
// round-tripped and validated offline (see tests/test_net.cpp). The byte layouts
// are cross-checked against CMaNGOS realmd (sAuthLogonChallenge_C / _S, AuthCodes.h)
// and the GTKer / wowdev wire docs; see docs/research/3-online-login-world-entry.md.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"

namespace wf {
namespace net {

// eAuthCmd -- the single leading opcode byte (CMaNGOS realmd/AuthCodes.h).
enum class AuthCmd : uint8_t {
    LOGON_CHALLENGE     = 0x00,
    LOGON_PROOF         = 0x01,
    RECONNECT_CHALLENGE = 0x02,
    RECONNECT_PROOF     = 0x03,
    REALM_LIST          = 0x10,
    XFER_INITIATE       = 0x30,
    XFER_DATA           = 0x31,
};

// AuthLogonResult -- the result byte in challenge/proof replies.
enum class AuthResult : uint8_t {
    SUCCESS            = 0x00,
    BANNED             = 0x03,
    UNKNOWN_ACCOUNT    = 0x04,
    INCORRECT_PASSWORD = 0x05,
    ALREADY_ONLINE     = 0x06,
    VERSION_INVALID    = 0x09,
    VERSION_UPDATE     = 0x0A,
    SUSPENDED          = 0x0C,
};

// ---- CMD_AUTH_LOGON_CHALLENGE (client -> server) ----------------------------
// The 4-char tags (gamename/platform/os/country) are little-endian fourCC, so on
// the wire they look reversed ("WoW\0" stays "WoW\0", "x86\0" -> "68x\0", etc.).
struct LogonChallengeRequest {
    uint8_t     version[3] = {1, 12, 1};
    uint16_t    build      = 5875;
    std::string platform   = "x86";   // written reversed as the fourCC tag
    std::string os         = "Win";
    std::string country    = "enUS";
    uint32_t    timezoneBias = 0;
    uint32_t    ip           = 0;     // client IP, big-endian on the wire
    std::string account;              // UPPERCASED ASCII, max 16 chars
};

// Reverse-and-pad a string into a 4-byte little-endian fourCC tag.
inline void writeFourCC(ByteWriter& w, const std::string& s) {
    char tag[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < s.size() && i < 4; ++i) tag[i] = s[i];
    // On the wire the client stores the fourCC as a little-endian u32, which
    // makes the visible byte order reversed.
    for (int i = 3; i >= 0; --i) w.u8(static_cast<uint8_t>(tag[i]));
}

inline std::vector<uint8_t> encodeLogonChallenge(const LogonChallengeRequest& r) {
    // Build the body first so we can prefix it with its length. Body layout
    // (CMaNGOS sAuthLogonChallenge_C): gamename[4], version[3], build[2],
    // platform[4], os[4], country[4], timezone_bias[4], ip[4], I_len[1], I[].
    ByteWriter body;
    writeFourCC(body, "WoW");          // gamename "WoW\0" -> on wire "\0WoW"
    body.u8(r.version[0]);
    body.u8(r.version[1]);
    body.u8(r.version[2]);
    body.u16(r.build);
    writeFourCC(body, r.platform);     // "x86" -> "68x\0"
    writeFourCC(body, r.os);           // "Win" -> "niW\0"
    writeFourCC(body, r.country);      // "enUS" -> "SUne"
    body.u32(r.timezoneBias);
    // IP is big-endian on the wire.
    body.u8(static_cast<uint8_t>((r.ip >> 24) & 0xFF));
    body.u8(static_cast<uint8_t>((r.ip >> 16) & 0xFF));
    body.u8(static_cast<uint8_t>((r.ip >> 8) & 0xFF));
    body.u8(static_cast<uint8_t>(r.ip & 0xFF));
    body.u8(static_cast<uint8_t>(r.account.size()));
    for (char c : r.account) body.u8(static_cast<uint8_t>(c));

    ByteWriter w;
    w.u8(static_cast<uint8_t>(AuthCmd::LOGON_CHALLENGE));
    w.u8(0x03);                                  // protocol-version / "error" constant
    w.u16(static_cast<uint16_t>(body.size()));   // remaining-body length (LE)
    w.bytes(body.data().data(), body.size());
    return w.take();
}

// Decode CMD_AUTH_LOGON_CHALLENGE (client -> server). Mirror of the encoder so
// a stub realmd can read what the logon client sent (offline round-trip test).
inline LogonChallengeRequest decodeLogonChallenge(const std::vector<uint8_t>& buf) {
    LogonChallengeRequest out;
    ByteReader r(buf);
    r.u8();                                       // cmd
    r.u8();                                       // protocol-version constant
    r.u16();                                      // body size
    r.skip(4);                                    // gamename fourCC
    out.version[0] = r.u8();
    out.version[1] = r.u8();
    out.version[2] = r.u8();
    out.build = r.u16();
    r.skip(4);                                    // platform fourCC
    r.skip(4);                                    // os fourCC
    r.skip(4);                                    // country fourCC
    out.timezoneBias = r.u32();
    out.ip = (static_cast<uint32_t>(r.u8()) << 24) |
             (static_cast<uint32_t>(r.u8()) << 16) |
             (static_cast<uint32_t>(r.u8()) << 8)  |
              static_cast<uint32_t>(r.u8());
    uint8_t ilen = r.u8();
    for (uint8_t i = 0; i < ilen; ++i) out.account += static_cast<char>(r.u8());
    return out;
}

// The server challenge reply.
struct LogonChallengeReply {
    AuthResult result = AuthResult::SUCCESS;
    std::vector<uint8_t> B;          // 32, server public ephemeral (LE)
    std::vector<uint8_t> N;          // 32, safe prime (LE)
    uint8_t              g = 7;
    std::vector<uint8_t> salt;       // 32 (LE)
    std::vector<uint8_t> crcSalt;    // 16 (version challenge; zeros ok)
    uint8_t              securityFlags = 0;
    bool                 ok = false; // parse succeeded with SUCCESS result
};

inline std::vector<uint8_t> encodeLogonChallengeReply(const LogonChallengeReply& r) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(AuthCmd::LOGON_CHALLENGE));
    w.u8(0x00);                                  // error
    w.u8(static_cast<uint8_t>(r.result));
    if (r.result != AuthResult::SUCCESS) return w.take();  // failure: stops here
    w.bytes(r.B.data(), r.B.size());             // 32
    w.u8(1);                                      // g_len
    w.u8(r.g);                                    // g (7)
    w.u8(32);                                      // N_len
    w.bytes(r.N.data(), r.N.size());             // 32
    w.bytes(r.salt.data(), r.salt.size());       // 32
    std::vector<uint8_t> crc = r.crcSalt;
    crc.resize(16, 0);
    w.bytes(crc.data(), 16);
    w.u8(r.securityFlags);
    return w.take();
}

inline LogonChallengeReply decodeLogonChallengeReply(const std::vector<uint8_t>& buf) {
    LogonChallengeReply out;
    ByteReader r(buf);
    if (r.u8() != static_cast<uint8_t>(AuthCmd::LOGON_CHALLENGE)) return out;
    r.u8();                                       // error byte
    out.result = static_cast<AuthResult>(r.u8());
    if (out.result != AuthResult::SUCCESS) { out.ok = false; return out; }
    out.B.resize(32);   for (auto& b : out.B) b = r.u8();
    uint8_t glen = r.u8();
    out.g = (glen >= 1) ? r.u8() : 7;
    for (uint8_t i = 1; i < glen; ++i) r.u8();    // tolerate longer g (never in 1.12)
    uint8_t nlen = r.u8();
    out.N.resize(nlen);  for (auto& b : out.N) b = r.u8();
    out.salt.resize(32); for (auto& b : out.salt) b = r.u8();
    out.crcSalt.resize(16); for (auto& b : out.crcSalt) b = r.u8();
    if (r.remaining() >= 1) out.securityFlags = r.u8();
    out.ok = true;
    return out;
}

// ---- CMD_AUTH_LOGON_PROOF (client -> server) --------------------------------
struct LogonProofRequest {
    std::vector<uint8_t> A;          // 32 (LE)
    std::vector<uint8_t> M1;         // 20
    std::vector<uint8_t> crcHash;    // 20 (emus ignore; zeros ok)
};

inline std::vector<uint8_t> encodeLogonProof(const LogonProofRequest& p) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(AuthCmd::LOGON_PROOF));
    w.bytes(p.A.data(), p.A.size());             // 32
    w.bytes(p.M1.data(), p.M1.size());           // 20
    std::vector<uint8_t> crc = p.crcHash;
    crc.resize(20, 0);
    w.bytes(crc.data(), 20);                     // 20
    w.u8(0);                                      // number_of_keys
    w.u8(0);                                      // security_flags
    return w.take();
}

inline LogonProofRequest decodeLogonProof(const std::vector<uint8_t>& buf) {
    LogonProofRequest p;
    ByteReader r(buf);
    r.u8();                                       // cmd
    p.A.resize(32);  for (auto& b : p.A) b = r.u8();
    p.M1.resize(20); for (auto& b : p.M1) b = r.u8();
    p.crcHash.resize(20); for (auto& b : p.crcHash) b = r.u8();
    return p;
}

// The server proof reply. Vanilla sends cmd + error + M2[20] (and sometimes a
// trailing u16/u32 unk). We read M2 defensively without depending on the tail.
struct LogonProofReply {
    AuthResult result = AuthResult::SUCCESS;
    std::vector<uint8_t> M2;         // 20
    bool ok = false;
};

inline std::vector<uint8_t> encodeLogonProofReply(const LogonProofReply& r) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(AuthCmd::LOGON_PROOF));
    w.u8(static_cast<uint8_t>(r.result));
    if (r.result != AuthResult::SUCCESS) return w.take();
    w.bytes(r.M2.data(), r.M2.size());           // 20
    w.u32(0);                                     // account flags (vanilla tolerated)
    w.u16(0);                                     // unk
    return w.take();
}

inline LogonProofReply decodeLogonProofReply(const std::vector<uint8_t>& buf) {
    LogonProofReply out;
    ByteReader r(buf);
    r.u8();                                       // cmd
    out.result = static_cast<AuthResult>(r.u8());
    if (out.result != AuthResult::SUCCESS) { out.ok = false; return out; }
    out.M2.resize(20); for (auto& b : out.M2) b = r.u8();
    out.ok = true;
    return out;
}

// ---- CMD_REALM_LIST ---------------------------------------------------------
inline std::vector<uint8_t> encodeRealmListRequest() {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(AuthCmd::REALM_LIST));
    w.u32(0);                                     // padding
    return w.take();
}

struct Realm {
    uint32_t    icon       = 0;       // 0 normal,1 PvP,6 RP,8 RP-PvP
    uint8_t     flags      = 0;       // 0x01 invalid,0x02 offline,0x04 specify-build
    std::string name;
    std::string address;              // "host:port"
    float       population = 0.0f;
    uint8_t     characters = 0;       // chars on this realm for this account
    uint8_t     timezone   = 0;
    uint8_t     id         = 0;
};

inline std::vector<uint8_t> encodeRealmListReply(const std::vector<Realm>& realms) {
    ByteWriter body;
    body.u32(0);                                  // unused
    body.u8(static_cast<uint8_t>(realms.size())); // vanilla: u8 count
    for (const Realm& rm : realms) {
        body.u32(rm.icon);
        body.u8(rm.flags);
        for (char c : rm.name) body.u8(static_cast<uint8_t>(c));
        body.u8(0);                               // nul
        for (char c : rm.address) body.u8(static_cast<uint8_t>(c));
        body.u8(0);                               // nul
        body.f32(rm.population);
        body.u8(rm.characters);
        body.u8(rm.timezone);
        body.u8(rm.id);
    }
    body.u16(0);                                  // trailing unused

    ByteWriter w;
    w.u8(static_cast<uint8_t>(AuthCmd::REALM_LIST));
    w.u16(static_cast<uint16_t>(body.size()));    // packet_size (LE)
    w.bytes(body.data().data(), body.size());
    return w.take();
}

inline std::string readCString(ByteReader& r) {
    std::string s;
    while (r.remaining() > 0) {
        uint8_t c = r.u8();
        if (c == 0) break;
        s += static_cast<char>(c);
    }
    return s;
}

inline std::vector<Realm> decodeRealmListReply(const std::vector<uint8_t>& buf) {
    std::vector<Realm> realms;
    ByteReader r(buf);
    if (r.u8() != static_cast<uint8_t>(AuthCmd::REALM_LIST)) return realms;
    r.u16();                                      // packet_size
    r.u32();                                      // unused
    uint8_t count = r.u8();                       // vanilla u8 count
    for (uint8_t i = 0; i < count; ++i) {
        Realm rm;
        rm.icon       = r.u32();
        rm.flags      = r.u8();
        rm.name       = readCString(r);
        rm.address    = readCString(r);
        rm.population = r.f32();
        rm.characters = r.u8();
        rm.timezone   = r.u8();
        rm.id         = r.u8();
        realms.push_back(std::move(rm));
    }
    return realms;
}

} // namespace net
} // namespace wf
