#include "test.hpp"
#include "crypto.hpp"
#include "srp6.hpp"
#include "worldproto.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace wf;

namespace {
std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 0xF]; }
    return s;
}
std::vector<uint8_t> bytes(std::initializer_list<int> v) {
    std::vector<uint8_t> b; for (int x : v) b.push_back((uint8_t)x); return b;
}
} // namespace

void test_proto() {
    std::printf("[proto]\n");

    // ---- SHA-1 against the standard vectors ----
    {
        std::string t = "test";
        auto h = sha1(std::vector<uint8_t>(t.begin(), t.end()));
        CHECK(hex(std::vector<uint8_t>(h.begin(), h.end())) ==
              "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
        auto h2 = sha1(bytes({0x53, 0x51}));
        CHECK(hex(std::vector<uint8_t>(h2.begin(), h2.end())) ==
              "0c3d7a19ac7c627290bf031ec3df76277b0f7f75");
    }

    // ---- BigUInt modpow against hand-checked + large values ----
    {
        // 7^10 mod 13 = 4 (verified by hand / python).
        BigUInt r = BigUInt::modpow(BigUInt(7), BigUInt(10), BigUInt(13));
        CHECK(r == BigUInt(4));

        // Round-trip byte conversion of the WoW prime.
        const BigUInt& N = Srp6::N();
        CHECK(hex(N.toBytesBE(32)) ==
              "894b645e89e1535bbdad5b8b290650530801b18ebfbf5e8fab3c82872a3e9bb7");

        // g^x mod N for x = 0x1234, cross-checked against python pow(7, 0x1234, N).
        BigUInt v = BigUInt::modpow(Srp6::g(), BigUInt::fromHexBE("1234"), N);
        CHECK(hex(v.toBytesBE(32)) ==
              "3be12a624a3b5e2c9b30760cee09d062733a408627a71c7af1b5ca4e4a4ac0ea");
    }

    // ---- full SRP6 handshake: client and server agree on K, proofs verify ----
    {
        std::string user = "ALICE", pass = "PASSWORD123";
        std::vector<uint8_t> salt(32), a(32), b(32);
        for (int i = 0; i < 32; ++i) { salt[i] = (uint8_t)(i*7+1); a[i] = (uint8_t)(i*3+5); b[i] = (uint8_t)(i*5+9); }

        Bytes verifier = Srp6::passwordVerifier(user, pass, salt);

        Srp6Server server(user, verifier, salt, b);
        Srp6Client client(user, pass, salt, a);

        Bytes ckey, cM1; client.process(server.publicB(), ckey, cM1);
        Bytes skey, sM1; bool ok = server.process(client.publicA(), skey, sM1);

        CHECK(ok);
        CHECK(ckey == skey);          // identical session key derived independently
        CHECK(cM1 == sM1);            // server recomputes the same client proof
        CHECK(ckey.size() == 40);

        // Server proof verifies on the client.
        Bytes sProof = Srp6Server::serverProof(client.publicA(), cM1, ckey);
        Bytes expect = Srp6Server::serverProof(client.publicA(), cM1, skey);
        CHECK(sProof == expect);

        // A wrong password must NOT agree.
        Srp6Client bad(user, "WRONGPASS", salt, a);
        Bytes bkey, bM1; bad.process(server.publicB(), bkey, bM1);
        CHECK(bM1 != sM1);
    }

    // ---- vanilla header cipher: encrypt then decrypt round-trips ----
    {
        Bytes key(40);
        for (int i = 0; i < 40; ++i) key[i] = (uint8_t)(i*11 + 3);
        WorldHeaderCrypt sender(key), receiver(key);

        std::vector<uint8_t> h1 = writeServerHeader(SMSG_AUTH_CHALLENGE, 6);
        std::vector<uint8_t> orig = h1;
        sender.encryptSend(h1.data(), h1.size());
        CHECK(h1 != orig);            // actually encrypted
        receiver.decryptRecv(h1.data(), h1.size());
        CHECK(h1 == orig);            // recovered

        // A second header continues the rolling state correctly.
        std::vector<uint8_t> h2 = writeServerHeader(SMSG_MONSTER_MOVE, 40);
        std::vector<uint8_t> orig2 = h2;
        sender.encryptSend(h2.data(), h2.size());
        receiver.decryptRecv(h2.data(), h2.size());
        CHECK(h2 == orig2);
    }

    // ---- framing round-trips ----
    {
        auto sh = writeServerHeader(SMSG_AUTH_RESPONSE, 100);
        ServerHeader s = readServerHeader(sh.data());
        CHECK(s.opcode == SMSG_AUTH_RESPONSE && s.payloadLen == 100);

        auto ch = writeClientHeader(CMSG_AUTH_SESSION, 200);
        ClientHeader c = readClientHeader(ch.data());
        CHECK(c.opcode == CMSG_AUTH_SESSION && c.payloadLen == 200);
    }
}
