#pragma once
// ---------------------------------------------------------------------------
// Camera: a WASD + mouse-look fly camera in WoW Z-up world space (the Noggit /
// game-style navigation, not an orbit cam). yaw rotates about +Z, pitch tilts;
// view() / proj() feed the renderer, and the gizmo, identically. Header-only so
// both the editor and tests use it; the math is unit-tested.
// ---------------------------------------------------------------------------
#include <cmath>
#include "math.hpp"

namespace wf::editor {

struct Camera {
    Vec3   eye{ 150.0f, -120.0f, 130.0f };
    float  yaw   = 0.9f;     // radians about +Z
    float  pitch = -0.5f;    // radians, + looks up
    // farZ spans many ADT tiles (533 yd each) so the low-res WDL horizon stays
    // inside the frustum; the software rasteriser has no depth-precision cost.
    double fovY  = 55.0, nearZ = 1.0, farZ = 40000.0;

    Vec3 forward() const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        return normalize(Vec3{ cp * std::cos(yaw), cp * std::sin(yaw), sp });
    }
    Vec3 right() const { return normalize(cross(forward(), Vec3{0, 0, 1})); }
    Vec3 up()    const { return cross(right(), forward()); }

    Mat4 view() const { return Mat4::lookAt(eye, eye + forward(), Vec3{0, 0, 1}); }
    Mat4 proj(float aspect) const { return Mat4::perspective(fovY, aspect, nearZ, farZ); }

    // Move along the camera basis (forward / right / world-up).
    void fly(float dForward, float dRight, float dUp) {
        eye += forward() * dForward + right() * dRight + Vec3{0, 0, 1} * dUp;
    }
    // Mouse-look (radians); pitch clamped just shy of vertical.
    void look(float dYaw, float dPitch) {
        yaw += dYaw;
        pitch += dPitch;
        const float lim = 1.55f;
        if (pitch >  lim) pitch =  lim;
        if (pitch < -lim) pitch = -lim;
    }
};

} // namespace wf::editor
