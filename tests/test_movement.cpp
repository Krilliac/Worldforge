// ---------------------------------------------------------------------------
// MovementInfo / MSG_MOVE codec round-trip tests (src/net/movement.hpp). No
// sockets: build a MovementInfo, write it, read it back, and assert every field
// survives for each flag combination, plus the packed-guid relay wrapper.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/movement.hpp"
#include "byte_reader.hpp"
#include "byte_writer.hpp"

using namespace wf;
using namespace wf::net;   // MOVEFLAG_* bits live here (shared with update_object)

namespace {

// Assert the flag-gated fields of two MovementInfos match (only the fields the
// flags say are present are meaningful after a read).
void checkEq(const MovementInfo& a, const MovementInfo& b) {
    CHECK(a.flags == b.flags && a.time == b.time);
    CHECK_APPROX(a.pos.x, b.pos.x); CHECK_APPROX(a.pos.y, b.pos.y);
    CHECK_APPROX(a.pos.z, b.pos.z); CHECK_APPROX(a.o, b.o);
    CHECK_APPROX(a.fallTime, b.fallTime);
    if (a.flags & MOVEFLAG_ONTRANSPORT) {
        CHECK(a.transportGuid == b.transportGuid && a.transportTime == b.transportTime);
        CHECK_APPROX(a.transportPos.x, b.transportPos.x);
        CHECK_APPROX(a.transportO, b.transportO);
    }
    if (a.flags & MOVEFLAG_SWIMMING) CHECK_APPROX(a.pitch, b.pitch);
    if (a.flags & MOVEFLAG_JUMPING) {
        CHECK_APPROX(a.jumpVelocity, b.jumpVelocity);
        CHECK_APPROX(a.jumpSin, b.jumpSin);
        CHECK_APPROX(a.jumpCos, b.jumpCos);
        CHECK_APPROX(a.jumpXYSpeed, b.jumpXYSpeed);
    }
    if (a.flags & MOVEFLAG_SPLINE_ELEVATION) CHECK_APPROX(a.splineElevation, b.splineElevation);
}

MovementInfo roundTrip(const MovementInfo& in) {
    ByteWriter w;
    writeMovementInfo(w, in);
    ByteReader r(w.data().data(), w.data().size());
    MovementInfo out = readMovementInfo(r);
    CHECK(r.remaining() == 0);   // consumed exactly what was written
    return out;
}

}  // namespace

void test_movement() {
    std::printf("[net.movement]\n");

    // Plain move (no conditional blocks): the common heartbeat/walk case.
    {
        MovementInfo m;
        m.flags = 0; m.time = 12345;
        m.pos = { 100.5f, -200.25f, 42.0f }; m.o = 3.14159f;
        m.fallTime = 0.5f;
        checkEq(m, roundTrip(m));
        // Exactly 8 (flags+time) + 16 (pos+o) + 4 (fallTime) = 28 bytes.
        ByteWriter w; writeMovementInfo(w, m);
        CHECK(w.data().size() == 28);
    }

    // Swimming: adds a pitch float before fallTime.
    {
        MovementInfo m;
        m.flags = MOVEFLAG_SWIMMING; m.time = 7;
        m.pos = { 1, 2, 3 }; m.o = 0.5f;
        m.pitch = -0.3f; m.fallTime = 1.0f;
        checkEq(m, roundTrip(m));
        ByteWriter w; writeMovementInfo(w, m);
        CHECK(w.data().size() == 28 + 4);        // + pitch
    }

    // Jumping: adds the 4-float jump block after fallTime.
    {
        MovementInfo m;
        m.flags = MOVEFLAG_JUMPING; m.time = 99;
        m.pos = { 5, 6, 7 }; m.o = 1.0f; m.fallTime = 250.0f;
        m.jumpVelocity = 7.95f; m.jumpSin = 0.5f; m.jumpCos = 0.866f; m.jumpXYSpeed = 4.5f;
        checkEq(m, roundTrip(m));
        ByteWriter w; writeMovementInfo(w, m);
        CHECK(w.data().size() == 28 + 16);       // + jump block
    }

    // On transport: raw-u64 guid + transport pos/o + time.
    {
        MovementInfo m;
        m.flags = MOVEFLAG_ONTRANSPORT; m.time = 1;
        m.pos = { 0, 0, 0 }; m.o = 0;
        m.transportGuid = 0x1122334455667788ull;
        m.transportPos = { 10, 20, 30 }; m.transportO = 2.0f; m.transportTime = 555;
        m.fallTime = 0;
        MovementInfo out = roundTrip(m);
        checkEq(m, out);
        CHECK(out.transportGuid == 0x1122334455667788ull);
        ByteWriter w; writeMovementInfo(w, m);
        CHECK(w.data().size() == 28 + 8 + 12 + 4 + 4);  // guid + tpos + to + ttime
    }

    // All flags at once: fields stay in the right order end-to-end.
    {
        MovementInfo m;
        m.flags = MOVEFLAG_ONTRANSPORT | MOVEFLAG_SWIMMING | MOVEFLAG_JUMPING
                | MOVEFLAG_SPLINE_ELEVATION;
        m.time = 0xDEADBEEF; m.pos = { -1, -2, -3 }; m.o = 6.28f;
        m.transportGuid = 0xFF00FF00ull; m.transportPos = { 9, 8, 7 };
        m.transportO = 1.1f; m.transportTime = 42;
        m.pitch = 0.25f; m.fallTime = 3.0f;
        m.jumpVelocity = 1; m.jumpSin = 2; m.jumpCos = 3; m.jumpXYSpeed = 4;
        m.splineElevation = 99.0f;
        checkEq(m, roundTrip(m));
    }

    // Packed-guid write/read parity (incl. interior zero bytes -> omitted).
    {
        for (uint64_t g : { uint64_t(0), uint64_t(1), uint64_t(0xFF),
                            uint64_t(0x00FF00FF00ull), uint64_t(0xFFFFFFFFFFFFFFFFull) }) {
            ByteWriter w; writePackedGuid(w, g);
            ByteReader r(w.data().data(), w.data().size());
            CHECK(readPackedGuid(r) == g && r.remaining() == 0);
        }
        // Zero guid packs to a single mask byte of 0.
        ByteWriter z; writePackedGuid(z, 0);
        CHECK(z.data().size() == 1 && z.data()[0] == 0);
    }

    // Server relay wrapper: packed mover guid + MovementInfo.
    {
        MoveRelay in;
        in.guid = 0xABCDEF01ull;
        in.info.flags = MOVEFLAG_SWIMMING; in.info.time = 500;
        in.info.pos = { 12, 34, 56 }; in.info.o = 1.5f; in.info.pitch = 0.1f;
        in.info.fallTime = 2.0f;
        std::vector<uint8_t> bytes = writeMoveRelay(in);
        ByteReader r(bytes.data(), bytes.size());
        MoveRelay out = readMoveRelay(r);
        CHECK(out.guid == in.guid && r.remaining() == 0);
        checkEq(in.info, out.info);
    }

    // Opcode-family predicate.
    {
        CHECK(isMovementOpcode(MSG_MOVE_HEARTBEAT));
        CHECK(isMovementOpcode(MSG_MOVE_START_FORWARD));
        CHECK(isMovementOpcode(MSG_MOVE_SET_FACING));
        CHECK(!isMovementOpcode(SMSG_UPDATE_OBJECT));
        CHECK(!isMovementOpcode(SMSG_MONSTER_MOVE));
    }
}
