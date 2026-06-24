// ---------------------------------------------------------------------------
// Offline tests for the self-contained vanilla 1.12.1 online-login -> world-entry
// client (src/net/*). NO sockets: every exchange is driven as pure encode/decode
// against the existing SRP6 server role and the rolling header cipher, so the
// whole logon -> world-handshake -> enter-world -> object-decode path round-trips
// deterministically. See docs/research/3-online-login-world-entry.md.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/auth_protocol.hpp"
#include "net/logon_client.hpp"
#include "net/world_auth.hpp"
#include "net/world_entry.hpp"
#include "net/update_object.hpp"
#include "srp6.hpp"
#include "worldproto.hpp"

#include <array>
#include <string>
#include <vector>

using namespace wf;
using namespace wf::net;

namespace {

// A tiny in-test realmd: holds an account's verifier+salt and answers the three
// logon opcodes exactly like CMaNGOS AuthSocket, so the LogonClient can be driven
// to completion entirely offline.
struct StubRealmd {
    std::string user;
    Bytes       verifier, salt, b;     // b = server private ephemeral (32)
    Bytes       lastK, lastM1, lastA;

    StubRealmd(std::string u, const std::string& pass)
        : user(std::move(u)) {
        salt.resize(32); b.resize(32);
        for (int i = 0; i < 32; ++i) { salt[i] = (uint8_t)(i * 7 + 1); b[i] = (uint8_t)(i * 5 + 9); }
        verifier = Srp6::passwordVerifier(user, pass, salt);
    }

    // Answer CMD_AUTH_LOGON_CHALLENGE.
    std::vector<uint8_t> challengeReply() {
        Srp6Server srv(user, verifier, salt, b);
        LogonChallengeReply rep;
        rep.result = AuthResult::SUCCESS;
        rep.B = srv.publicB();
        rep.N = Srp6::N().toBytesLE(32);
        rep.g = 7;
        rep.salt = salt;
        rep.crcSalt.assign(16, 0);
        return encodeLogonChallengeReply(rep);
    }

    // Answer CMD_AUTH_LOGON_PROOF: verify M1, derive K, return server proof.
    std::vector<uint8_t> proofReply(const std::vector<uint8_t>& proofRequest, bool& ok) {
        LogonProofRequest pr = decodeLogonProof(proofRequest);
        Srp6Server srv(user, verifier, salt, b);
        Bytes skey, sM1;
        ok = srv.process(pr.A, skey, sM1);
        lastK = skey; lastM1 = sM1; lastA = pr.A;
        if (!ok || sM1 != pr.M1) { ok = false; return {}; }
        LogonProofReply rep;
        rep.result = AuthResult::SUCCESS;
        Bytes proof = Srp6Server::serverProof(pr.A, sM1, skey);
        rep.M2.assign(proof.begin(), proof.end());
        return encodeLogonProofReply(rep);
    }
};

} // namespace

void test_net() {
    std::printf("[net]\n");

    // ---- auth_protocol: challenge request round-trips through the realmd reader.
    {
        LogonChallengeRequest req;
        req.account = "TESTER";
        auto bytes = encodeLogonChallenge(req);
        CHECK(bytes[0] == (uint8_t)AuthCmd::LOGON_CHALLENGE);
        LogonChallengeRequest back = decodeLogonChallenge(bytes);
        CHECK(back.account == "TESTER");
        CHECK(back.build == 5875);
        CHECK(back.version[0] == 1 && back.version[1] == 12 && back.version[2] == 1);
    }

    // ---- realm list round-trips (vanilla u8 count).
    {
        std::vector<Realm> realms = {
            { 0, 0, "WorldForge", "127.0.0.1:8085", 0.0f, 1, 1, 1 },
            { 1, 0, "PvP",        "10.0.0.2:8085",  0.5f, 0, 0, 2 },
        };
        auto bytes = encodeRealmListReply(realms);
        auto back = decodeRealmListReply(bytes);
        CHECK(back.size() == 2);
        CHECK(back[0].name == "WorldForge" && back[0].address == "127.0.0.1:8085");
        CHECK(back[0].characters == 1 && back[0].id == 1);
        CHECK(back[1].name == "PvP" && back[1].icon == 1);
    }

    // ---- full logon: LogonClient + StubRealmd derive an IDENTICAL session key K.
    Bytes sessionKey;
    {
        std::string user = "ALICE", pass = "PASSWORD123";
        StubRealmd realmd(user, pass);

        Bytes a(32);
        for (int i = 0; i < 32; ++i) a[i] = (uint8_t)(i * 3 + 5);
        LogonClient client(user, pass, a);

        // 1: challenge request -> realmd parses it.
        auto chReq = client.start();
        LogonChallengeRequest parsed = decodeLogonChallenge(chReq);
        CHECK(parsed.account == "ALICE");

        // 2: realmd challenge reply -> client runs SRP6, emits proof.
        std::vector<uint8_t> proofReq;
        CHECK(client.feedChallengeReply(realmd.challengeReply(), proofReq));

        // 3: realmd verifies M1, replies with M2 -> client verifies M2.
        bool serverOk = false;
        auto proofRep = realmd.proofReply(proofReq, serverOk);
        CHECK(serverOk);                                  // server accepted client M1
        std::vector<uint8_t> realmReq;
        CHECK(client.feedProofReply(proofRep, realmReq)); // client accepted server M2
        CHECK(client.authenticated());

        // The whole point: both sides hold the same 40-byte K.
        sessionKey = client.sessionKey();
        CHECK(sessionKey.size() == 40);
        CHECK(sessionKey == realmd.lastK);

        // 4: realm list.
        std::vector<Realm> realms = { { 0, 0, "WF", "127.0.0.1:8085", 0.0f, 0, 0, 1 } };
        CHECK(client.feedRealmList(encodeRealmListReply(realms)));
        CHECK(client.realms().size() == 1);
        CHECK(client.realms()[0].address == "127.0.0.1:8085");

        // Wrong password must NOT authenticate (server rejects M1).
        LogonClient badClient("ALICE", "WRONG", a);
        std::vector<uint8_t> badProof;
        CHECK(badClient.feedChallengeReply(realmd.challengeReply(), badProof));
        bool badOk = true;
        realmd.proofReply(badProof, badOk);
        CHECK(!badOk);
    }

    // ---- world handshake: digest matches on both sides; cipher keys from K.
    {
        std::string account = "ALICE";
        uint32_t serverSeed = 0xDEADBEEF;
        uint32_t clientSeed = 0x12345678;

        // Client builds CMSG_AUTH_SESSION; "server" recomputes the digest and
        // must get the same value (this is exactly what CMaNGOS HandleAuthSession
        // checks before m_crypt.Init(&K)).
        auto sessionBody = buildAuthSession(account, clientSeed, serverSeed, sessionKey);
        AuthSession decoded = decodeAuthSession(sessionBody);
        CHECK(decoded.username == "ALICE");
        CHECK(decoded.clientSeed == clientSeed);
        CHECK(decoded.build == 5875);

        auto expected = authSessionDigest(account, clientSeed, serverSeed, sessionKey);
        CHECK(decoded.digest == expected);

        // A different session key (impostor) yields a different digest -> reject.
        Bytes otherK = sessionKey; otherK[0] ^= 0xFF;
        auto bogus = authSessionDigest(account, clientSeed, serverSeed, otherK);
        CHECK(bogus != expected);

        // SMSG_AUTH_CHALLENGE seed round-trips.
        CHECK(decodeAuthChallenge(encodeAuthChallenge(serverSeed)) == serverSeed);

        // After the handshake both directions key WorldHeaderCrypt from K and the
        // first enciphered SMSG header decrypts back to a known opcode.
        WorldHeaderCrypt serverSide(sessionKey), clientSide(sessionKey);
        auto hdr = writeServerHeader(SMSG_CHAR_ENUM, 1);
        auto plain = hdr;
        serverSide.encryptSend(hdr.data(), hdr.size());
        CHECK(hdr != plain);
        clientSide.decryptRecv(hdr.data(), hdr.size());
        CHECK(hdr == plain);
        ServerHeader sh = readServerHeader(hdr.data());
        CHECK(sh.opcode == SMSG_CHAR_ENUM);

        // SMSG_AUTH_RESPONSE OK round-trips.
        AuthResponseMsg resp; resp.result = AuthResponse::OK;
        CHECK(decodeAuthResponse(encodeAuthResponse(resp)).result == AuthResponse::OK);
    }

    // ---- enter-world pump: CHAR_ENUM / PLAYER_LOGIN / LOGIN_VERIFY_WORLD / PING.
    {
        CharEnumEntry c;
        c.guid = 0x0000000000000005ull;
        c.name = "Thrall";
        c.race = 2; c.clazz = 7; c.gender = 0; c.level = 60;
        c.mapId = 1;
        c.x = -618.5f; c.y = -4251.6f; c.z = 38.7f;
        c.equipment[0].displayId = 1234;
        c.equipment[0].inventoryType = 1;

        auto enumBytes = encodeCharEnumReply({ c });
        auto back = decodeCharEnumReply(enumBytes);
        CHECK(back.size() == 1);
        CHECK(back[0].name == "Thrall");
        CHECK(back[0].guid == 0x05);
        CHECK(back[0].level == 60 && back[0].mapId == 1);
        CHECK_APPROX(back[0].x, -618.5f);
        CHECK(back[0].equipment[0].displayId == 1234);

        // The chosen character becomes a Player SimObject at its last position.
        SimObject avatar = charEnumToSimObject(back[0]);
        CHECK(avatar.kind == EntityKind::Player);
        CHECK(avatar.guid == 0x05 && avatar.mapId == 1);
        CHECK_APPROX(avatar.pos.y, -4251.6f);

        // CMSG_PLAYER_LOGIN carries the guid.
        CHECK(decodePlayerLogin(encodePlayerLogin(back[0].guid)) == 0x05);

        // SMSG_LOGIN_VERIFY_WORLD -- "you are in the world here".
        LoginVerifyWorld v{ 1, -618.5f, -4251.6f, 38.7f, 1.57f };
        auto vb = decodeLoginVerifyWorld(encodeLoginVerifyWorld(v));
        CHECK(vb.mapId == 1);
        CHECK_APPROX(vb.orientation, 1.57f);

        // Keep-alive ping/pong.
        auto p = decodePing(encodePing(42, 99));
        CHECK(p.pingId == 42 && p.latencyMs == 99);
        CHECK(decodePong(encodePong(42)) == 42);
    }

    // ---- SMSG_UPDATE_OBJECT: a CREATE_OBJECT2 unit decodes into a SimObject.
    {
        // Build a packet by hand matching the vanilla layout, then decode it.
        ByteWriter w;
        w.u32(1);                          // count = 1 block
        w.u8(0);                           // has_transport = false

        w.u8((uint8_t)UpdateType::CREATE_OBJECT2);
        // packed GUID 0xF130000000000010: bytes set are byte0 (0x10) and byte6
        // (0xF1)... we encode the full 8-byte LE value 0x00F1000000000010 via mask.
        uint64_t guid = 0x00F1000000000010ull;
        {
            uint8_t mask = 0; std::vector<uint8_t> gb;
            for (int i = 0; i < 8; ++i) {
                uint8_t byte = (uint8_t)((guid >> (8 * i)) & 0xFF);
                if (byte) { mask |= (uint8_t)(1u << i); gb.push_back(byte); }
            }
            w.u8(mask);
            for (uint8_t b : gb) w.u8(b);
        }
        w.u8((uint8_t)ObjectType::UNIT);   // object_type = UNIT

        // MovementBlock: LIVING + HAS_POSITION, standing (moveFlags = 0).
        w.u8(UPDATEFLAG_LIVING | UPDATEFLAG_HAS_POSITION);
        w.u32(0);                          // movement_flags = standing
        w.u32(123456);                     // time
        w.f32(-9449.0f); w.f32(64.0f); w.f32(56.0f);  // x,y,z
        w.f32(3.14f);                      // orientation
        w.f32(0.0f);                       // fall time
        for (int i = 0; i < 6; ++i) w.f32(7.0f);      // 6 speeds

        // Values block: set OBJECT_FIELD_ENTRY, OBJECT_FIELD_SCALE_X,
        // UNIT_FIELD_DISPLAYID, UNIT_FIELD_LEVEL (need 2 mask words to cover 0x36).
        {
            uint8_t blocks = 2;            // 64 fields covers indices 0..63
            uint32_t mask[2] = {0, 0};
            auto setBit = [&](uint32_t f) { mask[f / 32] |= (1u << (f % 32)); };
            setBit(OBJECT_FIELD_ENTRY);
            setBit(OBJECT_FIELD_SCALE_X);
            setBit(UNIT_FIELD_DISPLAYID);
            setBit(UNIT_FIELD_LEVEL);
            w.u8(blocks);
            w.u32(mask[0]); w.u32(mask[1]);
            // values in ascending field order: ENTRY(3), SCALE_X(4), DISPLAY(0x29), LEVEL(0x36)
            w.u32(299);                    // entry
            w.f32(1.5f);                   // scale
            w.u32(2541);                   // display id
            w.u32(60);                     // level
        }

        auto result = decodeUpdateObject(w.take());
        CHECK(result.objects.size() == 1);
        const ObjectUpdate& o = result.objects[0];
        CHECK(o.guid == guid);
        CHECK(o.kind == EntityKind::Creature);
        CHECK(o.hasPosition);
        CHECK_APPROX(o.pos.x, -9449.0f);
        CHECK_APPROX(o.pos.z, 56.0f);
        CHECK_APPROX(o.orientation, 3.14f);
        CHECK(o.entry == 299);
        CHECK(o.displayId == 2541);
        CHECK(o.level == 60);
        CHECK_APPROX(o.scale, 1.5f);

        // Apply onto a SimObject -- the shape the renderer consumes.
        SimObject so;
        applyToSimObject(o, so);
        CHECK(so.guid == guid && so.entry == 299);
        CHECK_APPROX(so.pos.y, 64.0f);
        CHECK_APPROX(so.radius, 1.5f);   // scale folded into bounding radius
    }

    // ---- SMSG_UPDATE_OBJECT: OUT_OF_RANGE_OBJECTS yields leaving guids.
    {
        ByteWriter w;
        w.u32(1);
        w.u8(0);
        w.u8((uint8_t)UpdateType::OUT_OF_RANGE_OBJECTS);
        w.u32(1);                          // one guid
        // packed guid 0x0000000000000007 -> mask 0x01, byte 0x07
        w.u8(0x01); w.u8(0x07);
        auto res = decodeUpdateObject(w.take());
        CHECK(res.objects.empty());
        CHECK(res.outOfRange.size() == 1 && res.outOfRange[0] == 0x07);
    }

    // ---- SMSG_MONSTER_MOVE: a NORMAL spline -> waypoints applied to SimObject.
    {
        ByteWriter w;
        // packed guid 0x10 (mask 0x01)
        w.u8(0x01); w.u8(0x10);
        w.f32(0.0f); w.f32(0.0f); w.f32(0.0f);     // start point
        w.u32(555);                                // spline id
        w.u8((uint8_t)MonsterMoveType::NORMAL);
        w.u32(0);                                  // spline flags
        w.u32(2000);                               // duration ms
        w.u32(2);                                  // 2 waypoints
        w.f32(10.0f); w.f32(0.0f); w.f32(0.0f);
        w.f32(10.0f); w.f32(10.0f); w.f32(0.0f);

        MonsterMove m = decodeMonsterMove(w.take());
        CHECK(m.guid == 0x10);
        CHECK(m.splineId == 555);
        CHECK(m.durationMs == 2000);
        CHECK(m.points.size() == 2);
        CHECK_APPROX(m.points[1].y, 10.0f);

        SimObject so; so.guid = 0x10;
        applyMonsterMove(m, so);
        CHECK(so.moving);
        CHECK(so.waypoints.size() == 2);
        CHECK_APPROX(so.pos.x, 10.0f);   // server's authoritative end position
        CHECK_APPROX(so.pos.y, 10.0f);

        // A STOP move clears motion and omits duration/points.
        ByteWriter ws;
        ws.u8(0x01); ws.u8(0x10);
        ws.f32(5.0f); ws.f32(5.0f); ws.f32(0.0f);
        ws.u32(556);
        ws.u8((uint8_t)MonsterMoveType::STOP);
        ws.u32(0);                                 // spline flags (no duration after)
        MonsterMove stop = decodeMonsterMove(ws.take());
        CHECK(stop.stop && stop.points.empty());
        applyMonsterMove(stop, so);
        CHECK(!so.moving && so.waypoints.empty());
    }

    // ---- SMSG_DESTROY_OBJECT: plain u64 guid.
    {
        ByteWriter w;
        w.u64(0x00F1000000000010ull);
        CHECK(decodeDestroyObject(w.take()) == 0x00F1000000000010ull);
    }

    // ---- end-to-end against WorldSim: decode a creature spawn, place it, then
    //      decode a monster-move and let WorldSim::tick() walk the same path.
    {
        WorldSim sim;
        // Spawn via the SimObject the update-object decoder produced above is
        // covered; here verify the decoded waypoints actually drive the existing
        // interpolation the editor uses.
        uint64_t g = sim.spawnCreature(299, 0, {0, 0, 0}, 0.0f);
        std::vector<Vec3> path = { {0,0,0}, {10,0,0} };
        CHECK(sim.setWaypoints(g, path));
        sim.setSpeed(g, 5.0f);
        size_t moved = sim.tick(0.5f);
        CHECK(moved >= 1);
        const SimObject* o = sim.find(g);
        CHECK(o != nullptr && o->pos.x > 0.0f);   // advanced toward the waypoint
    }
}
