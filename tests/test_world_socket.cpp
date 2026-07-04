// ---------------------------------------------------------------------------
// Offline tests for the world-link frame pump + opcode dispatch (src/net/
// world_socket.hpp). NO sockets: a simulated server endpoint builds framed
// (and, post-auth, header-encrypted) byte streams, which are fed to the pump in
// adversarial fragment patterns to prove reassembly, header decryption, and
// dispatch all behave exactly as they would over a real TCP link.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/world_socket.hpp"
#include "worldproto.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace wf;

namespace {

// Build a server->client frame the way mangosd would: 4-byte server header
// (encrypted with `srv` when non-null) followed by the plaintext body.
std::vector<uint8_t> makeServerFrame(uint16_t opcode, const std::vector<uint8_t>& body,
                                     WorldHeaderCrypt* srv) {
    std::vector<uint8_t> h = writeServerHeader(opcode, (uint32_t)body.size());
    if (srv) srv->encryptSend(h.data(), h.size());
    h.insert(h.end(), body.begin(), body.end());
    return h;
}

}  // namespace

void test_world_socket() {
    std::printf("[net.world_socket]\n");

    const std::vector<uint8_t> B1 = { 1, 2, 3, 4, 5 };
    const std::vector<uint8_t> B2 = {};                       // empty body
    const std::vector<uint8_t> B3(300, 0xAB);                 // spans >255, tests LE size

    // --- plaintext reassembly: one feed carrying several whole frames ---------
    {
        std::vector<uint8_t> stream;
        for (auto& f : { makeServerFrame(SMSG_AUTH_CHALLENGE, B1, nullptr),
                         makeServerFrame(SMSG_PONG, B2, nullptr),
                         makeServerFrame(SMSG_UPDATE_OBJECT, B3, nullptr) })
            stream.insert(stream.end(), f.begin(), f.end());

        WorldFramePump pump;
        std::vector<ServerFrame> out;
        CHECK(pump.feed(stream.data(), stream.size(), out));
        CHECK(out.size() == 3);
        CHECK(out[0].opcode == SMSG_AUTH_CHALLENGE && out[0].body == B1);
        CHECK(out[1].opcode == SMSG_PONG && out[1].body.empty());
        CHECK(out[2].opcode == SMSG_UPDATE_OBJECT && out[2].body == B3);
        CHECK(pump.pending() == 0);
    }

    // --- byte-at-a-time delivery: header split from body, split mid-header ----
    {
        std::vector<uint8_t> stream;
        for (auto& f : { makeServerFrame(SMSG_UPDATE_OBJECT, B3, nullptr),
                         makeServerFrame(SMSG_AUTH_CHALLENGE, B1, nullptr) })
            stream.insert(stream.end(), f.begin(), f.end());

        WorldFramePump pump;
        std::vector<ServerFrame> out;
        for (uint8_t b : stream) {                            // one byte per feed
            CHECK(pump.feed(&b, 1, out));
        }
        CHECK(out.size() == 2);
        CHECK(out[0].opcode == SMSG_UPDATE_OBJECT && out[0].body == B3);
        CHECK(out[1].opcode == SMSG_AUTH_CHALLENGE && out[1].body == B1);
    }

    // --- two frames arriving glued, then split at an awkward offset -----------
    {
        std::vector<uint8_t> a = makeServerFrame(SMSG_PONG, B1, nullptr);
        std::vector<uint8_t> b = makeServerFrame(SMSG_AUTH_RESPONSE, B1, nullptr);
        std::vector<uint8_t> stream = a;
        stream.insert(stream.end(), b.begin(), b.end());

        WorldFramePump pump;
        std::vector<ServerFrame> out;
        size_t cut = a.size() + 2;                            // mid second header
        CHECK(pump.feed(stream.data(), cut, out));
        CHECK(out.size() == 1 && out[0].opcode == SMSG_PONG);
        CHECK(pump.feed(stream.data() + cut, stream.size() - cut, out));
        CHECK(out.size() == 2 && out[1].opcode == SMSG_AUTH_RESPONSE);
    }

    // --- header cipher: server encrypts headers, pump decrypts them -----------
    // pump.recv-state must mirror the server's send-state (same key, both from 0).
    {
        const std::vector<uint8_t> key(40, 0x5A);
        WorldHeaderCrypt srv(key);                            // simulated server
        WorldFramePump pump;
        pump.activateCipher(key);
        CHECK(pump.cipherActive());

        std::vector<uint8_t> stream;
        for (auto& f : { makeServerFrame(SMSG_AUTH_RESPONSE, B1, &srv),
                         makeServerFrame(SMSG_UPDATE_OBJECT, B3, &srv),
                         makeServerFrame(SMSG_MONSTER_MOVE, B2, &srv) })
            stream.insert(stream.end(), f.begin(), f.end());

        std::vector<ServerFrame> out;
        // Deliver in 7-byte chunks so encrypted headers straddle feed boundaries.
        for (size_t i = 0; i < stream.size(); i += 7)
            CHECK(pump.feed(stream.data() + i, std::min<size_t>(7, stream.size() - i), out));
        CHECK(out.size() == 3);
        CHECK(out[0].opcode == SMSG_AUTH_RESPONSE && out[0].body == B1);
        CHECK(out[1].opcode == SMSG_UPDATE_OBJECT && out[1].body == B3);
        CHECK(out[2].opcode == SMSG_MONSTER_MOVE && out[2].body.empty());
    }

    // --- client encode: plaintext before auth, encrypted after ----------------
    {
        const std::vector<uint8_t> key(40, 0x33);
        WorldFramePump pump;

        // Pre-cipher CMSG_AUTH_SESSION: header is plaintext, decodes directly.
        std::vector<uint8_t> body = { 9, 8, 7 };
        std::vector<uint8_t> plain = pump.encodeClient(CMSG_AUTH_SESSION, body);
        ClientHeader ph = readClientHeader(plain.data());
        CHECK(ph.opcode == CMSG_AUTH_SESSION && ph.payloadLen == body.size());
        CHECK(std::equal(body.begin(), body.end(), plain.begin() + 6));

        // After activateCipher, the client header is encrypted; a matching server
        // crypt (recv-state mirrors the pump's send-state) recovers it.
        pump.activateCipher(key);
        WorldHeaderCrypt srvRecv(key);
        std::vector<uint8_t> enc = pump.encodeClient(CMSG_PLAYER_LOGIN, body);
        CHECK(enc.size() == 6 + body.size());
        uint8_t hdr[6];
        for (int i = 0; i < 6; ++i) hdr[i] = enc[i];
        srvRecv.decryptRecv(hdr, 6);                          // server decrypts header
        ClientHeader ch = readClientHeader(hdr);
        CHECK(ch.opcode == CMSG_PLAYER_LOGIN && ch.payloadLen == body.size());
        CHECK(std::equal(body.begin(), body.end(), enc.begin() + 6));  // body plaintext
    }

    // --- opcode dispatch ------------------------------------------------------
    {
        OpcodeDispatcher disp;
        int updates = 0, pongs = 0;
        std::vector<uint8_t> lastBody;
        disp.on(SMSG_UPDATE_OBJECT, [&](const ServerFrame& f) { ++updates; lastBody = f.body; });
        disp.on(SMSG_PONG, [&](const ServerFrame&) { ++pongs; });
        CHECK(disp.count() == 2 && disp.has(SMSG_PONG));

        CHECK(disp.dispatch(ServerFrame{ SMSG_UPDATE_OBJECT, B3 }));
        CHECK(disp.dispatch(ServerFrame{ SMSG_PONG, {} }));
        CHECK(!disp.dispatch(ServerFrame{ SMSG_WEATHER, {} }));   // no handler
        CHECK(updates == 1 && pongs == 1 && lastBody == B3);
    }

    // --- framing error: a malformed short header is rejected, not underflowed --
    {
        WorldFramePump pump;
        // size field < 2 makes payloadLen = size-2 underflow to a huge value; the
        // cap must catch it instead of trying to buffer ~4 GiB. (A uint16 size can
        // never legitimately exceed 0xFFFF, so this short-header case is the real
        // malformed-frame guard.)
        std::vector<uint8_t> bad = { 0x00, 0x00, 0x00, 0x00 };   // size = 0
        std::vector<ServerFrame> out;
        CHECK(!pump.feed(bad.data(), bad.size(), out));
        CHECK(out.empty());
    }
}
