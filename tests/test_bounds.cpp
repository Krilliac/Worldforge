#include "test.hpp"
#include "bounds.hpp"
#include "m2.hpp"
#include "wmo.hpp"
#include "math.hpp"

#include <vector>

using namespace wf;

void test_bounds() {
    std::printf("[bounds]\n");

    // --- Aabb basics + expand ------------------------------------------------
    Aabb b;
    CHECK(!b.valid());                                   // empty -> invalid
    b.expand({1, 2, 3});
    b.expand({-1, 5, -3});
    CHECK(b.valid());
    CHECK_APPROX(b.min.x, -1.0f); CHECK_APPROX(b.max.y, 5.0f);
    CHECK_APPROX(b.center().z, 0.0f);                    // (3 + -3)/2
    CHECK_APPROX(b.half().y, 1.5f);                      // (5 - 2)/2
    CHECK(b.radius() > 0.0f);

    // --- aabbOfPoints --------------------------------------------------------
    Aabb p = aabbOfPoints({ {0,0,0}, {10,0,0}, {0,4,0}, {0,0,6} });
    CHECK_APPROX(p.max.x, 10.0f);
    CHECK_APPROX(p.max.y, 4.0f);
    CHECK_APPROX(p.max.z, 6.0f);
    CHECK(aabbOfPoints({}).valid() == false);            // no points -> invalid

    // --- modelBounds(M2): from the vertex pool ------------------------------
    M2Model m;
    m.vertices.resize(3);
    m.vertices[0].pos = {-2, 0, 0};
    m.vertices[1].pos = { 2, 0, 0};
    m.vertices[2].pos = { 0, 1, 3};
    Aabb mb = modelBounds(m);
    CHECK(mb.valid());
    CHECK_APPROX(mb.min.x, -2.0f);
    CHECK_APPROX(mb.max.x,  2.0f);
    CHECK_APPROX(mb.max.z,  3.0f);
    // A long object's enclosing-sphere radius exceeds half its longest axis.
    CHECK(mb.radius() >= 2.0f);

    // --- wmoBounds: straight from the root bbox -----------------------------
    WmoRoot root;
    root.bboxMin = {-30, -10, 0};
    root.bboxMax = { 30,  10, 25};
    Aabb wb = wmoBounds(root);
    CHECK(wb.valid());
    CHECK_APPROX(wb.half().x, 30.0f);
    CHECK_APPROX(wb.max.z, 25.0f);

    WmoRoot empty;   // default bbox is degenerate -> invalid
    CHECK(!wmoBounds(empty).valid());
}
