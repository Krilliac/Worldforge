#include "test.hpp"
#include "math.hpp"

using namespace wf;

void test_math() {
    std::printf("[math]\n");

    // identity * v = v
    {
        Mat4 I = Mat4::identity();
        Vec4 v = I * Vec4{1, 2, 3, 1};
        CHECK_APPROX(v.x, 1); CHECK_APPROX(v.y, 2); CHECK_APPROX(v.z, 3);
    }

    // translate moves a point
    {
        Mat4 T = Mat4::translate({10, -5, 2});
        Vec4 v = T * Vec4{1, 1, 1, 1};
        CHECK_APPROX(v.x, 11); CHECK_APPROX(v.y, -4); CHECK_APPROX(v.z, 3);
    }

    // composition is right-to-left: (T * S) scales then translates
    {
        Mat4 T = Mat4::translate({1, 0, 0});
        Mat4 S = Mat4::scale({2, 2, 2});
        Vec4 v = (T * S) * Vec4{1, 0, 0, 1};   // scale -> (2,0,0), translate -> (3,0,0)
        CHECK_APPROX(v.x, 3);
    }

    // rotateZ(90) maps +X to +Y
    {
        Mat4 R = Mat4::rotateZ(90);
        Vec4 v = R * Vec4{1, 0, 0, 1};
        CHECK_APPROX(v.x, 0); CHECK_APPROX(v.y, 1); CHECK_APPROX(v.z, 0);
    }

    // cross / dot
    {
        Vec3 c = cross({1, 0, 0}, {0, 1, 0});
        CHECK_APPROX(c.z, 1);
        CHECK_APPROX(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}), 32);
    }

    // lookAt: a point at the camera target lands on the -Z axis in eye space
    {
        Mat4 V = Mat4::lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
        Vec4 e = V * Vec4{0, 0, 0, 1};
        CHECK_APPROX(e.x, 0); CHECK_APPROX(e.y, 0); CHECK_APPROX(e.z, -10);
    }

    // perspective: a point on the near plane centre maps to clip z = -w (NDC -1)
    {
        double zn = 1.0, zf = 100.0;
        Mat4 P = Mat4::perspective(60.0, 1.0, zn, zf);
        Vec4 clip = P * Vec4{0, 0, static_cast<float>(-zn), 1};
        CHECK_APPROX(clip.z / clip.w, -1.0);
        Vec4 farp = P * Vec4{0, 0, static_cast<float>(-zf), 1};
        CHECK_APPROX(farp.z / farp.w, 1.0);
    }
}
