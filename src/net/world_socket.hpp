#pragma once
// ---------------------------------------------------------------------------
// World-link frame pump + opcode dispatch for vanilla 1.12.1 (build 5875).
//
// The pure codecs already exist: framing (writeClientHeader / readServerHeader),
// the rolling header cipher (WorldHeaderCrypt), and the per-opcode encode/decode
// in the sibling headers. What was missing is the *wiring* that sits between a
// byte stream and those codecs:
//
//   * TCP delivers arbitrary fragments, so server frames must be reassembled
//     across reads -- a header may split from its body, two frames may arrive in
//     one read, a header may itself be split mid-way.
//   * The rolling cipher only ever touches the 4-byte *server header* (the body
//     is plaintext), advances its state exactly once per header byte, and does
//     not engage until AFTER the client sends CMSG_AUTH_SESSION.
//   * Decoded frames need to reach the right handler.
//
// WorldFramePump owns the receive-side reassembly + header decryption and yields
// whole {opcode, body} frames; encodeClient() builds an outgoing client frame
// with the header encrypted iff the cipher is live. Both are PURE over byte
// buffers -- no sockets -- so the whole segmented, encrypted exchange is unit
// tested offline (see tests/test_net.cpp), including headers split across feeds.
// A thin socket driver (BridgeClient-style winsock) just pushes recv() bytes in
// and writes encodeClient() bytes out.
//
// Role: WorldForge is the *client* (it connects to mangosd), so the pump reads
// 4-byte server headers and writes 6-byte client headers.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "worldproto.hpp"   // Opcode, WorldHeaderCrypt, {read,write}*Header

namespace wf {

// One decoded server frame: the opcode and its plaintext body (header stripped).
struct ServerFrame {
    uint16_t             opcode = 0;
    std::vector<uint8_t> body;
};

class WorldFramePump {
public:
    // Feed raw bytes read from the socket. Every complete server frame now
    // available is appended to `out` (there may be zero, one, or many). Returns
    // false on a framing error (a header claiming payloadLen past a sane cap),
    // after which the caller should drop the connection; the pump is then left
    // in an unspecified state and must not be reused.
    bool feed(const uint8_t* data, size_t len, std::vector<ServerFrame>& out) {
        rx_.insert(rx_.end(), data, data + len);
        for (;;) {
            if (!haveHeader_) {
                if (rx_.size() < kServerHdr) return true;      // wait for a header
                uint8_t hb[kServerHdr];
                for (size_t i = 0; i < kServerHdr; ++i) hb[i] = rx_[i];
                if (crypt_) crypt_->decryptRecv(hb, kServerHdr);  // headers only
                hdr_ = readServerHeader(hb);
                if (hdr_.payloadLen > kMaxPayload) return false;  // bogus size
                rx_.erase(rx_.begin(), rx_.begin() + kServerHdr);
                haveHeader_ = true;
            }
            if (rx_.size() < hdr_.payloadLen) return true;     // wait for the body
            ServerFrame f;
            f.opcode = hdr_.opcode;
            f.body.assign(rx_.begin(), rx_.begin() + hdr_.payloadLen);
            rx_.erase(rx_.begin(), rx_.begin() + hdr_.payloadLen);
            haveHeader_ = false;
            out.push_back(std::move(f));
        }
    }

    // Engage the rolling header cipher (both directions) seeded from the 40-byte
    // SRP6 session key. Call once, right after sending CMSG_AUTH_SESSION: from
    // then on every server header is decrypted and every client header encrypted.
    // Before this, headers are plaintext (the auth challenge/session exchange).
    void activateCipher(const std::vector<uint8_t>& sessionKey) {
        crypt_ = std::make_unique<WorldHeaderCrypt>(sessionKey);
    }

    bool cipherActive() const { return crypt_ != nullptr; }

    // Build an outgoing client frame: 6-byte client header + body, header
    // encrypted iff the cipher is live. `body` may be empty (e.g. CMSG_CHAR_ENUM).
    std::vector<uint8_t> encodeClient(uint32_t opcode, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> out = writeClientHeader(opcode, static_cast<uint32_t>(body.size()));
        if (crypt_) crypt_->encryptSend(out.data(), out.size());   // 6-byte header
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    // Bytes buffered but not yet formed into a frame (diagnostics / tests).
    size_t pending() const { return rx_.size(); }

private:
    static constexpr size_t kServerHdr  = 4;
    static constexpr uint32_t kMaxPayload = 0x20000;   // 128 KiB: no vanilla frame nears this

    std::vector<uint8_t>              rx_;              // reassembly buffer
    bool                              haveHeader_ = false;
    ServerHeader                      hdr_{};
    std::unique_ptr<WorldHeaderCrypt> crypt_;          // null until activateCipher()
};

// Minimal opcode -> handler router. Register handlers for the opcodes you care
// about; dispatch() calls the matching one (if any) and reports whether it was
// handled, so callers can log unrecognised opcodes. Handlers see the plaintext
// body; they typically hand it to the matching decoder in the sibling headers.
class OpcodeDispatcher {
public:
    using Handler = std::function<void(const ServerFrame&)>;

    void on(uint16_t opcode, Handler h) { handlers_[opcode] = std::move(h); }

    // Returns true if a handler ran for this frame's opcode.
    bool dispatch(const ServerFrame& f) const {
        auto it = handlers_.find(f.opcode);
        if (it == handlers_.end()) return false;
        it->second(f);
        return true;
    }

    bool has(uint16_t opcode) const { return handlers_.count(opcode) != 0; }
    size_t count() const { return handlers_.size(); }

private:
    std::unordered_map<uint16_t, Handler> handlers_;
};

}  // namespace wf
