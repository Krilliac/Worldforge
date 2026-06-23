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
}
