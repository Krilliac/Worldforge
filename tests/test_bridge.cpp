#include "test.hpp"
#include "editor_bridge.hpp"
#include "db_export.hpp"

#include <string>
#include <vector>

using namespace wf;

void test_bridge() {
    std::printf("[bridge]\n");

    // --- MoveObject round-trips through frame + payload ----------------------
    MoveObject mv;
    mv.guid = 0xF130000000001234ull;
    mv.pos = {100.5f, -200.25f, 30.0f};
    mv.orientation = 3.14159f;
    mv.opId = 77;
    std::vector<uint8_t> packet = encode(mv);

    EditorFrame fr; size_t used = 0;
    CHECK(readFrame(packet, fr, used));
    CHECK(used == packet.size());
    CHECK(fr.opcode == EDITOR_MOVE_OBJECT);
    MoveObject back = decodeMoveObject(fr.payload);
    CHECK(back.guid == mv.guid);
    CHECK_APPROX(back.pos.x, 100.5f);
    CHECK_APPROX(back.pos.y, -200.25f);
    CHECK_APPROX(back.pos.z, 30.0f);
    CHECK_APPROX(back.orientation, 3.14159f);
    CHECK(back.opId == 77);

    // --- SpawnCreature -------------------------------------------------------
    SpawnCreature sp; sp.entry = 299; sp.mapId = 0; sp.pos = {1,2,3}; sp.orientation = 1.5f; sp.opId = 5;
    std::vector<uint8_t> spk = encode(sp);
    CHECK(readFrame(spk, fr, used) && fr.opcode == EDITOR_SPAWN_CREATURE);
    SpawnCreature sb = decodeSpawnCreature(fr.payload);
    CHECK(sb.entry == 299 && sb.opId == 5);
    CHECK_APPROX(sb.orientation, 1.5f);

    // --- Despawn + Ack -------------------------------------------------------
    Despawn dp; dp.guid = 42; dp.opId = 9;
    CHECK(readFrame(encode(dp), fr, used) && fr.opcode == EDITOR_DESPAWN);
    CHECK(decodeDespawn(fr.payload).guid == 42);

    Ack ack; ack.opId = 9; ack.status = 1;
    CHECK(readFrame(encode(ack), fr, used) && fr.opcode == EDITOR_ACK);
    Ack ab = decodeAck(fr.payload);
    CHECK(ab.opId == 9 && ab.status == 1);

    // --- SetWaypoints: variable-length path ----------------------------------
    SetWaypoints wp; wp.guid = 1000; wp.opId = 3;
    wp.path = { {0,0,0}, {10,0,0}, {10,10,0}, {0,10,0} };
    std::vector<uint8_t> wpk = encode(wp);
    CHECK(readFrame(wpk, fr, used) && fr.opcode == EDITOR_SET_WAYPOINTS);
    SetWaypoints wb = decodeSetWaypoints(fr.payload);
    CHECK(wb.guid == 1000 && wb.opId == 3);
    CHECK(wb.path.size() == 4);
    CHECK_APPROX(wb.path[2].x, 10.0f);
    CHECK_APPROX(wb.path[2].y, 10.0f);

    // --- framing: short read is rejected, two frames stream back-to-back -----
    EditorFrame tmp; size_t c2 = 0;
    std::vector<uint8_t> tooShort(packet.begin(), packet.begin() + 4);
    CHECK(!readFrame(tooShort, tmp, c2));               // header incomplete

    std::vector<uint8_t> twoFrames = encode(dp);
    std::vector<uint8_t> second = encode(ack);
    twoFrames.insert(twoFrames.end(), second.begin(), second.end());
    CHECK(readFrame(twoFrames, fr, used) && fr.opcode == EDITOR_DESPAWN);
    std::vector<uint8_t> rest(twoFrames.begin() + used, twoFrames.end());
    CHECK(readFrame(rest, fr, used) && fr.opcode == EDITOR_ACK);
}

void test_db_export() {
    std::printf("[db_export]\n");

    // --- creature INSERT -----------------------------------------------------
    CreatureSpawn c;
    c.guid = 50001; c.entry = 299; c.mapId = 0;
    c.pos = {-9449.0f, 64.0f, 56.0f}; c.orientation = 0.0f;
    c.spawnTimeSecs = 120; c.movementType = 1; c.spawnDist = 5.0f;
    std::string sql = creatureInsert(c);
    CHECK(sql.find("INSERT INTO creature") != std::string::npos);
    CHECK(sql.find("50001") != std::string::npos);
    CHECK(sql.find("299") != std::string::npos);
    CHECK(sql.find("-9449") != std::string::npos);
    CHECK(sql.back() == ';');

    // --- gameobject INSERT: orientation 0 -> rotation3 (cos 0) = 1 -----------
    GameObjectSpawn g;
    g.guid = 60001; g.entry = 1734; g.mapId = 0; g.pos = {1.0f, 2.0f, 3.0f};
    g.orientation = 0.0f; g.state = 1;
    std::string gsql = gameObjectInsert(g);
    CHECK(gsql.find("INSERT INTO gameobject") != std::string::npos);
    CHECK(gsql.find("rotation3") != std::string::npos);
    CHECK(gsql.find(", 0, 0, 0, 1,") != std::string::npos);  // rot0..3 for o=0

    // --- waypoint inserts ----------------------------------------------------
    std::vector<Vec3> path = { {10,10,0}, {20,10,0}, {20,20,0} };
    std::string wsql = waypointInserts(50001, path, 2000);
    CHECK(wsql.find("UPDATE creature SET MovementType = 2") != std::string::npos);
    CHECK(wsql.find("creature_movement") != std::string::npos);
    // Three nodes -> three INSERTs, points 1..3.
    size_t n = 0, at = 0;
    while ((at = wsql.find("INSERT INTO creature_movement", at)) != std::string::npos) { ++n; at += 1; }
    CHECK(n == 3);
    CHECK(wsql.find("2000") != std::string::npos);     // waittime present
}
