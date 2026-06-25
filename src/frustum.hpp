#pragma once
// ---------------------------------------------------------------------------
// View-frustum culling. Extracts the six clip-space planes from a combined
// view-projection matrix (Gribb-Hartmann) and answers a conservative
// AABB-visibility query so the renderer/editor can skip off-screen tiles,
// WMOs and doodads. Pure math over Mat4/Aabb, headless and unit-tested.
// ---------------------------------------------------------------------------
#include "math.hpp"
#include "bounds.hpp"

namespace wf {

// A plane stored as (n.x, n.y, n.z, d): a point p is on the positive (inside)
// side when dot(n, p) + d >= 0. Planes are normalized so d is a true signed
// distance, which makes radius/extent tests meaningful.
struct Plane {
    Vec3  n{0, 0, 0};
    float d = 0.0f;
    float signedDistance(const Vec3& p) const { return dot(n, p) + d; }
};

// Six planes, all with inward-facing normals: left, right, bottom, top,
// near, far (the Gribb-Hartmann order; the order itself is irrelevant to the
// cull test).
struct Frustum {
    Plane planes[6];
};

// Extract the frustum from a combined view-projection matrix (clip = viewProj
// * worldPos). Works for any projection that produces OpenGL [-1,1] clip space,
// which is what Mat4::perspective() in math.hpp builds.
Frustum makeFrustum(const Mat4& viewProj);

// Conservative visibility: returns false only when `box` lies entirely on the
// outside half-space of some plane (positive-vertex test). False positives are
// possible near corners (acceptable for culling); false negatives are not.
bool aabbVisible(const Frustum& f, const Aabb& box);

} // namespace wf
