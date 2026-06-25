#include "test.hpp"
#include "frustum.hpp"
#include "math.hpp"
#include "bounds.hpp"

#include <cstdio>

using namespace wf;

namespace {
// Small AABB centered at `c` with half-extent `h` on every axis.
Aabb boxAt(const Vec3& c, float h) {
    Aabb b;
    b.expand(c + Vec3{ h,  h,  h});
    b.expand(c + Vec3{-h, -h, -h});
    return b;
}
} // namespace

void test_frustum() {
    std::printf("[frustum]\n");

    // Z-up world (matches the engine's lookAt up-vector convention). Camera at
    // the origin looking down +X; the focus point is out along +X.
    Vec3 eye{0, 0, 0};
    Vec3 center{1, 0, 0};
    Mat4 view = Mat4::lookAt(eye, center, Vec3{0, 0, 1});
    Mat4 proj = Mat4::perspective(60.0, 1.0, 0.5, 100.0);
    Mat4 viewProj = proj * view;

    Frustum f = makeFrustum(viewProj);

    // Plane normals must face inward: a point well inside the frustum (straight
    // ahead, comfortably between near and far) is on the positive side of all
    // six planes. Catches a flipped Gribb-Hartmann sign.
    Vec3 inside{20, 0, 0};
    for (const Plane& pl : f.planes) CHECK(pl.signedDistance(inside) >= 0.0f);

    // 1) A box at the focus point ahead of the camera is visible.
    CHECK(aabbVisible(f, boxAt(Vec3{20, 0, 0}, 1.0f)));

    // 2) A box far BEHIND the camera (down -X) is culled by the near plane.
    CHECK(!aabbVisible(f, boxAt(Vec3{-50, 0, 0}, 1.0f)));

    // 3) A box far off to the SIDE, beyond the 60-degree FOV, is culled. At
    //    x=20 the half-FOV edge is ~11.5 units off-axis; y=200 is far outside.
    CHECK(!aabbVisible(f, boxAt(Vec3{20, 200, 0}, 1.0f)));
    CHECK(!aabbVisible(f, boxAt(Vec3{20, 0, 200}, 1.0f)));  // above, also outside

    // 4) A box beyond the far plane (z-depth > 100 along view dir) is culled.
    CHECK(!aabbVisible(f, boxAt(Vec3{500, 0, 0}, 1.0f)));

    // 5) A huge box that encloses the camera (and the whole frustum) is
    //    visible: the p-vertex lies on the inside of every plane.
    CHECK(aabbVisible(f, boxAt(Vec3{0, 0, 0}, 1000.0f)));

    // Edge case: a box straddling the near plane (partly behind, partly ahead)
    // is conservatively kept -- never falsely culled.
    CHECK(aabbVisible(f, boxAt(Vec3{0, 0, 0}, 2.0f)));

    // Sanity on the off-axis boundary: just inside the FOV cone stays visible,
    // clearly outside it does not. At x=20, tan(30deg)*20 ~= 11.5.
    CHECK(aabbVisible(f, boxAt(Vec3{20, 5, 0}, 0.5f)));
    CHECK(!aabbVisible(f, boxAt(Vec3{20, 40, 0}, 0.5f)));
}
