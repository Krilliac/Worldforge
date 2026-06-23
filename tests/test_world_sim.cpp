#include "test.hpp"
#include "server/world_sim.hpp"

#include <vector>

using namespace wf;

void test_world_sim() {
    std::printf("[world_sim]\n");

    WorldSim sim;
    CHECK(sim.aliveCount() == 0);

    uint64_t g = sim.spawnCreature(299, 0, {-9449.0f, 64.0f, 56.0f}, 0.0f);
    CHECK(sim.aliveCount() == 1);
    CHECK((g >> 48) == 0xF130);                   // UNIT high-guid
    const SimObject* o = sim.find(g);
    CHECK(o != nullptr && o->entry == 299);
    CHECK_APPROX(o->pos.x, -9449.0f);

    CHECK(sim.moveObject(g, {-9440.0f, 70.0f, 56.5f}, 1.5f));
    o = sim.find(g);
    CHECK_APPROX(o->pos.y, 70.0f);
    CHECK_APPROX(o->orientation, 1.5f);
    CHECK(!sim.moveObject(123456, {0,0,0}, 0));   // unknown guid

    std::vector<Vec3> path = { {0,0,0}, {10,0,0}, {10,10,0} };
    CHECK(sim.setWaypoints(g, path));
    CHECK(sim.find(g)->waypoints.size() == 3);

    // A second spawn gets a distinct guid.
    uint64_t g2 = sim.spawnCreature(300, 0, {0,0,0}, 0);
    CHECK(g2 != g && sim.aliveCount() == 2);
    CHECK(sim.guids().size() == 2);

    CHECK(sim.despawn(g));
    CHECK(sim.aliveCount() == 1 && sim.find(g) == nullptr);
    CHECK(!sim.despawn(g));                        // already gone

    sim.fx().weatherCount++;
    CHECK(sim.fx().weatherCount == 1);

    // --- movement: a creature patrols its waypoints, looping --------------
    WorldSim w;
    uint64_t c = w.spawnCreature(299, 0, {0, 0, 0}, 0.0f);
    CHECK(w.setSpeed(c, 10.0f));                       // 10 yd/s, easy arithmetic
    std::vector<Vec3> loop = { {0,0,0}, {20,0,0}, {20,20,0} };
    CHECK(w.setWaypoints(c, loop));

    // It starts on node 0, so the first tick skips that arrival and walks 5
    // yards along the +X leg toward node 1.
    CHECK(w.simTimeMs() == 0);
    w.tick(0.5f);                                      // 5 yards
    const SimObject* co = w.find(c);
    CHECK(co->moving);
    CHECK_APPROX(co->pos.x, 5.0f);
    CHECK_APPROX(co->pos.y, 0.0f);
    CHECK_APPROX(co->orientation, 0.0f);               // facing +X
    CHECK(w.simTimeMs() == 500);

    // Four more 5-yard steps: reach node 1 (x=20) and start up the +Y leg toward
    // node 2, so it is now facing +Y (~pi/2).
    for (int i = 0; i < 4; ++i) w.tick(0.5f);
    co = w.find(c);
    CHECK_APPROX(co->pos.x, 20.0f);
    CHECK_APPROX(co->pos.y, 5.0f);
    CHECK(co->orientation > 1.0f);                     // turned toward +Y

    // A lone creature with no path stays put and reports not moving.
    uint64_t s = w.spawnCreature(300, 0, {5,5,5}, 0.0f);
    w.tick(1.0f);
    CHECK(!w.find(s)->moving);
    CHECK_APPROX(w.find(s)->pos.x, 5.0f);

    // Players spawn in the PLAYER guid space and snapshot is GUID-sorted.
    uint64_t p = w.spawnPlayer(0, {1,2,3}, 0.0f, "Tester");
    CHECK((p >> 48) == 0);                             // low guid -> player
    CHECK(w.find(p)->kind == EntityKind::Player && w.find(p)->name == "Tester");

    // Bounds default per kind and are overridable (for model-sized picking).
    CHECK(w.find(s)->radius > 0.0f);                   // creature has a default
    CHECK(w.setBounds(s, 8.0f));
    CHECK_APPROX(w.find(s)->radius, 8.0f);
    CHECK(!w.setBounds(999999, 1.0f));                 // unknown guid
    std::vector<SimObject> snap = w.snapshot();
    CHECK(snap.size() == 3);
    for (size_t i = 1; i < snap.size(); ++i) CHECK(snap[i-1].guid < snap[i].guid);
}
