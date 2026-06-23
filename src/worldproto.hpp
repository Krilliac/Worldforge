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
