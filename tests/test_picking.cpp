#include "test.hpp"
#include "picking.hpp"
#include "world_view.hpp"
#include "terrain.hpp"
#include "math.hpp"

#include <vector>

using namespace wf;

namespace {
EntityState mk(uint64_t guid, Vec3 pos, float radius = 2.0f) {
    EntityState e; e.guid = guid; e.kind = 0; e.pos = pos; e.moving = true;
    e.boundingRadius = radius; return e;
}
// A flat quad on the z=0 plane spanning [-50,50] in x and y, two triangles.
Mesh flatGround() {
    Mesh m;
    m.vertices = {
        { Vec3{-50,-50,0}, Vec3{0,0,1} },
        { Vec3{ 50,-50,0}, Vec3{0,0,1} },
        { Vec3{ 50, 50,0}, Vec3{0,0,1} },
        { Vec3{-50, 50,0}, Vec3{0,0,1} },
    };
    m.indices = { 0,1,2, 0,2,3 };
    return m;
}
} // namespace

void test_picking() {
    std::printf("[picking]\n");

    // Camera at origin looking down +X (WoW north), east to the right, +Z up.
    const Vec3 eye{0,0,0}, fwd{1,0,0}, right{0,-1,0}, up{0,0,1};
    const float W = 800, H = 600;
    const double fov = 60.0, aspect = double(W)/H;

    // --- centre ray points straight along forward ---------------------------
    Ray c = screenRay(eye, fwd, right, up, fov, aspect, W/2, H/2, W, H);
    CHECK_APPROX(c.dir.x, 1.0f);
    CHECK(std::fabs(c.dir.y) < 1e-4f && std::fabs(c.dir.z) < 1e-4f);

    // --- ray vs sphere: a hit ahead, a miss to the side ---------------------
    CHECK_APPROX(raySphere(c, Vec3{10,0,0}, 2.0f), 8.0f);   // enters at x=8
    CHECK(raySphere(c, Vec3{10,20,0}, 2.0f) < 0.0f);        // off to the side
    CHECK(raySphere(c, Vec3{-10,0,0}, 2.0f) < 0.0f);        // behind the camera

    // --- ray vs triangle (shared gizmo.hpp helper) --------------------------
    float tt = -1.0f;
    CHECK(rayTriangle(c, Vec3{20,-5,-5}, Vec3{20,5,-5}, Vec3{20,0,8}, tt));
    CHECK_APPROX(tt, 20.0f);                                // plane x=20 ahead
    float tmiss = -1.0f;
    CHECK(!rayTriangle(c, Vec3{20,10,10}, Vec3{20,20,10}, Vec3{20,15,18}, tmiss)); // off-axis

    // --- pickEntity: nearest of several entities ----------------------------
    WorldView view;
    view.apply(mk(100, {30,0,0}));      // far, straight ahead
    view.apply(mk(200, {10,0,0}));      // near, straight ahead
    view.apply(mk(300, {15,40,0}));     // off to the side (a miss)
    PickResult pe = pickEntity(c, view, /*pad*/0.0f);
    CHECK(pe.hit() && pe.kind == PickResult::Kind::Entity);
    CHECK(pe.guid == 200);                                  // nearest wins
    CHECK_APPROX(pe.distance, 8.0f);                        // 10 - radius 2

    // --- per-entity bounds: a big object is clickable where a small one isn't -
    // Both sit 4 yards off the ray's axis at x=30.
    WorldView wbig;   wbig.apply(mk(1, {30,4,0}, 6.0f));    // big bounds -> hit
    CHECK(pickEntity(c, wbig, 0.0f).hit());
    WorldView wsmall; wsmall.apply(mk(2, {30,4,0}, 1.0f));  // small bounds -> miss
    CHECK(!pickEntity(c, wsmall, 0.0f).hit());
    // Unknown bounds (0) fall back to the provided default radius.
    WorldView wunk; wunk.apply(mk(3, {30,0,0}, 0.0f));
    PickResult fb = pickEntity(c, wunk, /*pad*/0.0f, /*fallback*/5.0f);
    CHECK(fb.hit() && fb.guid == 3);
    CHECK_APPROX(fb.distance, 25.0f);                       // 30 - fallback 5

    // --- a ray angled down hits the ground; pickTerrain returns the point ----
    Ray down = screenRay(eye + Vec3{0,0,20}, normalize(Vec3{1,0,-1}), right, up,
                         fov, aspect, W/2, H/2, W, H);
    PickResult pg = pickTerrain(down, flatGround());
    CHECK(pg.hit() && pg.kind == PickResult::Kind::Terrain);
    CHECK_APPROX(pg.point.z, 0.0f);                         // landed on z=0
    CHECK_APPROX(pg.point.x, 20.0f);                        // 20 ahead, 20 down

    // --- pick(): an entity in front of the ground is chosen over it ----------
    WorldView v2;
    v2.apply(mk(900, {20,0,0}));                            // sits on the ground line
    Mesh ground = flatGround();
    PickResult both = pick(c, v2, ground);
    CHECK(both.hit() && both.kind == PickResult::Kind::Entity && both.guid == 900);

    // With no entities, the same click falls through to terrain.
    WorldView empty;
    PickResult terr = pick(down, empty, ground, 3.0f);
    CHECK(terr.hit() && terr.kind == PickResult::Kind::Terrain);

    // A ray fired straight up from above the ground hits nothing.
    Ray sky = screenRay(Vec3{0,0,10}, normalize(Vec3{0,0,1}), right, Vec3{-1,0,0},
                        fov, aspect, W/2, H/2, W, H);
    CHECK(!pick(sky, empty, ground).hit());
}
