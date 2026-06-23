#include "test.hpp"
#include "editing.hpp"
#include "gizmo.hpp"

#include <vector>

using namespace wf;

void test_editing() {
    std::printf("[editing]\n");

    // --- falloff weights -----------------------------------------------------
    CHECK_APPROX(falloffWeight(Falloff::Linear, 0.0f, 10.0f), 1.0f);  // centre
    CHECK_APPROX(falloffWeight(Falloff::Linear, 10.0f, 10.0f), 0.0f); // edge
    CHECK_APPROX(falloffWeight(Falloff::Linear, 5.0f, 10.0f), 0.5f);  // midpoint
    CHECK(falloffWeight(Falloff::Smooth, 5.0f, 10.0f) > 0.0f);
    CHECK(falloffWeight(Falloff::Flat, 9.9f, 10.0f) == 1.0f);         // flat = full
    CHECK(falloffWeight(Falloff::Linear, 11.0f, 10.0f) == 0.0f);      // outside
    // innerRatio holds full strength inside the inner band.
    CHECK_APPROX(falloffWeight(Falloff::Linear, 4.0f, 10.0f, 0.5f), 1.0f);
    CHECK(falloffWeight(Falloff::Linear, 6.0f, 10.0f, 0.5f) < 1.0f);

    // --- raise / lower terrain ----------------------------------------------
    std::vector<Vertex> verts = {
        {{0, 0, 0}, {0, 0, 1}},      // under brush centre
        {{5, 0, 0}, {0, 0, 1}},      // partway out
        {{100, 0, 0}, {0, 0, 1}},    // far outside the radius
    };
    Brush b; b.center = {0, 0, 0}; b.radius = 10.0f; b.strength = 4.0f;
    b.falloff = Falloff::Linear;
    int hits = brushRaiseLower(verts, b, +1.0f);
    CHECK(hits == 2);                                  // the far vertex untouched
    CHECK_APPROX(verts[0].position.z, 4.0f);          // centre: full strength
    CHECK_APPROX(verts[1].position.z, 2.0f);          // half falloff -> 2.0
    CHECK_APPROX(verts[2].position.z, 0.0f);          // outside -> unchanged

    // Lower undoes the raise on the centre vertex.
    brushRaiseLower(verts, b, -1.0f);
    CHECK_APPROX(verts[0].position.z, 0.0f);

    // --- flatten toward a target height -------------------------------------
    std::vector<Vertex> hill = { {{0, 0, 10.0f}, {0,0,1}} };
    Brush fb; fb.center = {0,0,0}; fb.radius = 10.0f; fb.strength = 0.5f;
    fb.falloff = Falloff::Flat;
    brushFlatten(hill, fb, 0.0f);
    CHECK_APPROX(hill[0].position.z, 5.0f);           // halfway to target
    brushFlatten(hill, fb, 0.0f);
    CHECK_APPROX(hill[0].position.z, 2.5f);           // halfway again

    // --- paint alpha coverage -----------------------------------------------
    AlphaMap am;                                       // starts all-zero
    int painted = paintAlpha(am, 0.5f, 0.5f, 0.25f, 1.0f, Falloff::Flat, 255);
    CHECK(painted > 0);
    CHECK(am.at(32, 32) == 255);                       // centre fully painted
    CHECK(am.at(0, 0) == 0);                           // corner outside the brush
    // Half strength only moves halfway toward the target on a fresh map.
    AlphaMap am2;
    paintAlpha(am2, 0.5f, 0.5f, 0.25f, 0.5f, Falloff::Flat, 200);
    CHECK(am2.at(32, 32) == 100);
}

void test_gizmo() {
    std::printf("[gizmo]\n");

    // --- ray/plane: straight down onto the ground (z=0) ----------------------
    Ray down{ {0, 0, 50}, {0, 0, -1} };
    float t = 0;
    CHECK(rayPlane(down, {0,0,0}, {0,0,1}, t));
    CHECK_APPROX(t, 50.0f);
    Vec3 hit = down.origin + down.dir * t;
    CHECK_APPROX(hit.z, 0.0f);
    // Parallel ray misses.
    Ray flat{ {0,0,50}, {1,0,0} };
    CHECK(!rayPlane(flat, {0,0,0}, {0,0,1}, t));

    // --- ray/triangle --------------------------------------------------------
    Vec3 a{0,0,0}, bb{10,0,0}, c{0,10,0};
    Ray r{ {2, 2, 5}, {0, 0, -1} };
    CHECK(rayTriangle(r, a, bb, c, t));
    CHECK_APPROX(t, 5.0f);
    Ray miss{ {8, 8, 5}, {0, 0, -1} };                 // outside the triangle
    CHECK(!rayTriangle(miss, a, bb, c, t));

    // --- pick a terrain mesh -------------------------------------------------
    Mesh m;
    m.vertices = { {{0,0,0},{0,0,1}}, {{10,0,0},{0,0,1}},
                   {{0,10,0},{0,0,1}}, {{10,10,0},{0,0,1}} };
    m.indices = { 0,1,2, 1,3,2 };
    Ray pr{ {5, 5, 100}, {0, 0, -1} };
    Vec3 p;
    CHECK(pickMesh(pr, m, p));
    CHECK_APPROX(p.z, 0.0f);
    CHECK_APPROX(p.x, 5.0f);
    CHECK_APPROX(p.y, 5.0f);

    // --- camera ray: centre of screen looks straight along forward -----------
    Ray center = cameraRay({0,0,0}, {1,0,0}, {0,-1,0}, {0,0,1}, 60.0, 1.5, 0.0f, 0.0f);
    CHECK_APPROX(center.dir.x, 1.0f);
    CHECK_APPROX(center.dir.y, 0.0f);
    CHECK_APPROX(center.dir.z, 0.0f);

    // --- snapping ------------------------------------------------------------
    CHECK_APPROX(snap(7.3f, 5.0f), 5.0f);
    CHECK_APPROX(snap(8.0f, 5.0f), 10.0f);
    CHECK_APPROX(snap(3.0f, 0.0f), 3.0f);              // step 0 = no-op
    Vec3 sv = snap(Vec3{2.1f, 4.9f, 7.5f}, 1.0f);
    CHECK_APPROX(sv.x, 2.0f); CHECK_APPROX(sv.y, 5.0f); CHECK_APPROX(sv.z, 8.0f);

    // --- drag along an axis: a vertical pointer move slides along +Z ---------
    Ray from{ {0, -10, 0}, {0, 1, 0} };                // looking +Y at the origin
    Ray to  { {0, -10, 4}, {0, 1, 0} };                // same ray lifted +4 in Z
    Vec3 moved = dragAlongAxis({0,0,0}, {0,0,1}, from, to);
    CHECK_APPROX(moved.z, 4.0f);
    CHECK_APPROX(moved.x, 0.0f);
    CHECK_APPROX(moved.y, 0.0f);
}
