#include "test.hpp"
#include "editor_bridge.hpp"
#include "db_export.hpp"
#include "debugdraw.hpp"
#include "server/world_sim.hpp"

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

    // --- debug stream: marker round-trips (incl. label string) --------------
    DebugMarker dm;
    dm.type = DebugVisType::Height; dm.pos = {12.0f, 34.0f, 56.0f};
    dm.color = {0,255,0,255}; dm.value = 4.25f; dm.label = "groundZ";
    CHECK(readFrame(encode(dm), fr, used) && fr.opcode == EDITOR_DEBUG_MARKER);
    DebugMarker mb = decodeDebugMarker(fr.payload);
    CHECK(mb.type == DebugVisType::Height);
    CHECK_APPROX(mb.pos.z, 56.0f);
    CHECK_APPROX(mb.value, 4.25f);
    CHECK(mb.label == "groundZ");

    // --- debug line with a hit point (DV_LOS_BLOCK) -------------------------
    DebugLine dl;
    dl.type = DebugVisType::LosBlock; dl.from = {0,0,0}; dl.to = {10,0,5};
    dl.color = {255,0,0,255}; dl.hasHit = true; dl.hit = {6,0,3};
    CHECK(readFrame(encode(dl), fr, used) && fr.opcode == EDITOR_DEBUG_LINE);
    DebugLine lb = decodeDebugLine(fr.payload);
    CHECK(lb.type == DebugVisType::LosBlock && lb.hasHit);
    CHECK_APPROX(lb.hit.x, 6.0f);

    // --- debug path (DV_PATH) ----------------------------------------------
    DebugPath dpth; dpth.guid = 7; dpth.bad = false; dpth.color = {0,200,255,255};
    dpth.points = { {0,0,0}, {5,5,0}, {10,0,0} };
    CHECK(readFrame(encode(dpth), fr, used) && fr.opcode == EDITOR_DEBUG_PATH);
    DebugPath pb = decodeDebugPath(fr.payload);
    CHECK(pb.guid == 7 && pb.points.size() == 3);
    CHECK_APPROX(pb.points[1].x, 5.0f);

    // --- debug volume: a cell box and a trigger sphere ----------------------
    DebugVolume dv; dv.kind = 0; dv.type = DebugVisType::Cell;
    dv.center = {100,100,10}; dv.half = {16,16,8}; dv.color = {120,120,120,255};
    CHECK(readFrame(encode(dv), fr, used) && fr.opcode == EDITOR_DEBUG_VOLUME);
    DebugVolume vb = decodeDebugVolume(fr.payload);
    CHECK(vb.kind == 0 && vb.type == DebugVisType::Cell);
    CHECK_APPROX(vb.half.x, 16.0f);

    // --- category mapping + apply produces renderable primitives ------------
    CHECK(categoryFor(DebugVisType::LosOk)    == DebugCategory::LineOfSight);
    CHECK(categoryFor(DebugVisType::PathBad)  == DebugCategory::NavPath);
    CHECK(categoryFor(DebugVisType::HitPoint) == DebugCategory::HitPoint);

    DebugDraw dd;
    apply(dd, lb);   // a LoS line + a hit point
    CHECK(dd.categoryBuffers(DebugCategory::LineOfSight).lines.size() == 2);
    CHECK(dd.categoryBuffers(DebugCategory::HitPoint).points.size() == 1);
    apply(dd, vb);   // a cell box -> 12 edges
    CHECK(dd.categoryBuffers(DebugCategory::Cell).lines.size() == 24);

    // --- override-light RPC: scope + target round-trip ----------------------
    OverrideLight ol;
    ol.overrideLightId = 396; ol.fadeInMs = 3000;
    ol.scope = FxScope::Zone; ol.zoneId = 1519; ol.targetGuid = 0; ol.opId = 11;
    CHECK(readFrame(encode(ol), fr, used) && fr.opcode == EDITOR_OVERRIDE_LIGHT);
    OverrideLight ob = decodeOverrideLight(fr.payload);
    CHECK(ob.overrideLightId == 396 && ob.fadeInMs == 3000);
    CHECK(ob.scope == FxScope::Zone && ob.zoneId == 1519 && ob.opId == 11);

    OverrideLight olt;
    olt.overrideLightId = 1; olt.scope = FxScope::Target; olt.targetGuid = 0xF130000000000042ull;
    OverrideLight obt = decodeOverrideLight((readFrame(encode(olt), fr, used), fr.payload));
    CHECK(obt.scope == FxScope::Target && obt.targetGuid == 0xF130000000000042ull);

    // --- live entity stream: state + remove round-trip ----------------------
    EntityState es;
    es.guid = 0xF130000000000099ull; es.kind = 1; es.entry = 1234; es.mapId = 0;
    es.pos = {-9440.5f, 70.25f, 56.0f}; es.orientation = 2.5f;
    es.moving = true; es.speed = 7.0f; es.boundingRadius = 3.25f;
    es.aabbMin = {-2,-3,-1}; es.aabbMax = {2,3,9}; es.name = "Thrall";
    CHECK(readFrame(encode(es), fr, used) && fr.opcode == EDITOR_ENTITY_STATE);
    EntityState eb = decodeEntityState(fr.payload);
    CHECK(eb.guid == es.guid && eb.kind == 1 && eb.entry == 1234);
    CHECK_APPROX(eb.pos.x, -9440.5f);
    CHECK_APPROX(eb.orientation, 2.5f);
    CHECK(eb.moving && eb.name == "Thrall");
    CHECK_APPROX(eb.speed, 7.0f);
    CHECK_APPROX(eb.boundingRadius, 3.25f);
    CHECK(eb.hasBox());
    CHECK_APPROX(eb.aabbMin.y, -3.0f);
    CHECK_APPROX(eb.aabbMax.z, 9.0f);

    EntityRemove er; er.guid = es.guid;
    CHECK(readFrame(encode(er), fr, used) && fr.opcode == EDITOR_ENTITY_REMOVE);
    CHECK(decodeEntityRemove(fr.payload).guid == es.guid);

    // --- server runtime status round-trip -----------------------------------
    ServerStatus sst;
    sst.simTimeMs = 12345; sst.entityCount = 7;
    sst.weatherType = 2; sst.weatherGrade = 0.6f;
    sst.lastSound = 8960; sst.lastCinematic = 81; sst.lastOverrideLight = 396;
    sst.weatherCount = 3; sst.soundCount = 5; sst.cinematicCount = 1;
    sst.worldStateCount = 4; sst.lightCount = 2;
    CHECK(readFrame(encode(sst), fr, used) && fr.opcode == EDITOR_SERVER_STATE);
    ServerStatus sstb = decodeServerStatus(fr.payload);
    CHECK(sstb.simTimeMs == 12345 && sstb.entityCount == 7);
    CHECK(sstb.weatherType == 2);
    CHECK_APPROX(sstb.weatherGrade, 0.6f);
    CHECK(sstb.lastSound == 8960 && sstb.lastCinematic == 81 && sstb.lastOverrideLight == 396);
    CHECK(sstb.weatherCount == 3 && sstb.soundCount == 5 && sstb.cinematicCount == 1);
    CHECK(sstb.worldStateCount == 4 && sstb.lightCount == 2);

    // --- ReloadGrid: the derived-file hot-swap op (negative grid coords) -----
    ReloadGrid rg; rg.mapId = 1; rg.gx = -3; rg.gy = 60; rg.opId = 21;
    CHECK(readFrame(encode(rg), fr, used) && fr.opcode == EDITOR_RELOAD_GRID);
    ReloadGrid rgb;
    CHECK(decodeReloadGrid(fr.payload, rgb));
    CHECK(rgb.mapId == 1 && rgb.gx == -3 && rgb.gy == 60 && rgb.opId == 21);
    CHECK(!decodeReloadGrid(std::vector<uint8_t>(fr.payload.begin(),
                                                 fr.payload.end() - 1), rgb));

    // --- MarkPoints: a 100-point batch round-trips ----------------------------
    MarkPoints mp; mp.ttlMs = 15000; mp.opId = 22;
    for (int i = 0; i < 100; ++i)
        mp.points.push_back({ (float)i, (float)-i, 0.5f * (float)i });
    CHECK(readFrame(encode(mp), fr, used) && fr.opcode == EDITOR_MARK_POINTS);
    std::vector<uint8_t> mpPayload = fr.payload;      // kept for truncation tests
    MarkPoints mpb;
    CHECK(decodeMarkPoints(mpPayload, mpb));
    CHECK(mpb.points.size() == 100 && mpb.ttlMs == 15000 && mpb.opId == 22);
    CHECK_APPROX(mpb.points[99].x, 99.0f);
    CHECK_APPROX(mpb.points[99].z, 49.5f);

    // zero points is a valid encoding (a no-op the server still acks)
    MarkPoints none; none.ttlMs = 1000; none.opId = 23;
    CHECK(readFrame(encode(none), fr, used));
    MarkPoints noneb;
    CHECK(decodeMarkPoints(fr.payload, noneb));
    CHECK(noneb.points.empty() && noneb.ttlMs == 1000 && noneb.opId == 23);

    // a truncated payload fails cleanly instead of throwing / over-reading
    MarkPoints junk;
    CHECK(!decodeMarkPoints(std::vector<uint8_t>(mpPayload.begin(),
                                                 mpPayload.begin() + mpPayload.size() / 2), junk));
    CHECK(!decodeMarkPoints(std::vector<uint8_t>(mpPayload.begin(),
                                                 mpPayload.begin() + 4), junk));   // count only
    CHECK(!decodeMarkPoints({}, junk));                                            // empty

    // --- SqlApply: SQL + reload command round-trip ----------------------------
    SqlApply sq;
    sq.sql = "UPDATE creature_template SET minlevel = 5 WHERE entry = 299;";
    sq.reloadCommand = ".reload creature_template";
    sq.opId = 24;
    CHECK(readFrame(encode(sq), fr, used) && fr.opcode == EDITOR_SQL_APPLY);
    SqlApply sqb;
    CHECK(decodeSqlApply(fr.payload, sqb));
    CHECK(sqb.sql == sq.sql && sqb.reloadCommand == sq.reloadCommand && sqb.opId == 24);
    SqlApply sqjunk;
    CHECK(!decodeSqlApply(std::vector<uint8_t>{ 9, 0, 'x' }, sqjunk));   // lying length
    CHECK(!decodeSqlApply({}, sqjunk));

    // --- reload-command mapping (pure data) -----------------------------------
    CHECK(std::string(reloadCommandFor("creature_loot_template")) ==
          ".reload creature_loot_template");
    CHECK(std::string(reloadCommandFor("creature_template")) == ".reload creature_template");
    CHECK(std::string(reloadCommandFor("quest_template")) == ".reload quest_template");
    CHECK(reloadCommandFor("creature") == nullptr);      // spawn row: next grid load
    CHECK(reloadCommandFor("gameobject") == nullptr);    // spawn row: next grid load
    CHECK(reloadCommandFor("no_such_table") == nullptr);
    CHECK(reloadCommandFor("") == nullptr);

    // --- the WorldSim end of the new ops: frames in, logs + acks out ----------
    // (the verifiable core the stub server -- and, in spirit, the mangos-side
    // handlers -- sits on)
    WorldSim sim;
    ack = Ack{};

    // ReloadGrid lands in the reloadedGrids log and acks with the op's id.
    rg = ReloadGrid{}; rg.mapId = 0; rg.gx = 31; rg.gy = 48; rg.opId = 100;
    CHECK(readFrame(encode(rg), fr, used));
    CHECK(sim.handleEditorFrame(fr, ack));
    CHECK(ack.opId == 100 && ack.status == 0);
    CHECK(sim.reloadedGrids().size() == 1);
    CHECK(sim.reloadedGrids()[0].mapId == 0);
    CHECK(sim.reloadedGrids()[0].gx == 31 && sim.reloadedGrids()[0].gy == 48);

    // SqlApply lands in the appliedSql log.
    sq = SqlApply{}; sq.sql = "DELETE FROM creature WHERE guid = 7;";
    sq.opId = 101;
    CHECK(readFrame(encode(sq), fr, used));
    CHECK(sim.handleEditorFrame(fr, ack));
    CHECK(ack.opId == 101 && ack.status == 0);
    CHECK(sim.appliedSql().size() == 1);
    CHECK(sim.appliedSql()[0].sql == sq.sql);
    CHECK(sim.appliedSql()[0].reloadCommand.empty());

    // MarkPoints materialise as markers that expire against the sim clock.
    MarkPoints mk; mk.ttlMs = 1000; mk.opId = 102;
    mk.points = { {1,2,3}, {4,5,6} };
    CHECK(readFrame(encode(mk), fr, used));
    CHECK(sim.handleEditorFrame(fr, ack));
    CHECK(ack.opId == 102 && ack.status == 0);
    CHECK(sim.markers().size() == 2);
    CHECK_APPROX(sim.markers()[1].pos.y, 5.0f);
    sim.tick(0.5f);
    CHECK(sim.markers().size() == 2);        // 500 ms in: still alive
    sim.tick(0.6f);
    CHECK(sim.markers().empty());            // 1100 ms: past the 1 s ttl

    // Zero points: still handled + acked ok, adds nothing.
    MarkPoints zero; zero.ttlMs = 500; zero.opId = 103;
    CHECK(readFrame(encode(zero), fr, used));
    CHECK(sim.handleEditorFrame(fr, ack));
    CHECK(ack.opId == 103 && ack.status == 0);
    CHECK(sim.markers().empty());

    // A truncated MarkPoints payload is handled but error-acked, applying nothing.
    MarkPoints big; big.ttlMs = 1000; big.opId = 104;
    big.points = { {1,1,1}, {2,2,2}, {3,3,3} };
    CHECK(readFrame(encode(big), fr, used));
    fr.payload.resize(fr.payload.size() / 2);
    CHECK(sim.handleEditorFrame(fr, ack));
    CHECK(ack.status != 0);
    CHECK(sim.markers().empty());

    // Frames the sim doesn't own fall through to the caller.
    EditorFrame other; other.opcode = EDITOR_HELLO;
    CHECK(!sim.handleEditorFrame(other, ack));
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
