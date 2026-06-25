#include "frustum.hpp"

#include <cmath>

namespace wf {

namespace {
// Normalize a plane in place so its normal is unit length (and d scales with
// it), giving true signed distances. Degenerate (zero-length) normals are left
// as-is to avoid NaNs.
Plane normalizedPlane(float a, float b, float c, float d) {
    float len = std::sqrt(a*a + b*b + c*c);
    if (len > 1e-8f) { float inv = 1.0f / len; a *= inv; b *= inv; c *= inv; d *= inv; }
    return { Vec3{a, b, c}, d };
}
} // namespace

Frustum makeFrustum(const Mat4& vp) {
    // Gribb-Hartmann: for clip = vp * world and clip space x,y,z in [-w, w],
    // each plane is a sum/difference of matrix rows. With math.hpp's
    // column-major storage, row r is { at(r,0), at(r,1), at(r,2), at(r,3) }.
    // Row3 + Row0 gives the left plane (clip.x >= -clip.w), etc. The resulting
    // normals point INWARD, which is what aabbVisible() expects.
    auto row = [&](int r, int c) { return vp.at(r, c); };

    Frustum f;
    // left:   w + x >= 0
    f.planes[0] = normalizedPlane(row(3,0)+row(0,0), row(3,1)+row(0,1), row(3,2)+row(0,2), row(3,3)+row(0,3));
    // right:  w - x >= 0
    f.planes[1] = normalizedPlane(row(3,0)-row(0,0), row(3,1)-row(0,1), row(3,2)-row(0,2), row(3,3)-row(0,3));
    // bottom: w + y >= 0
    f.planes[2] = normalizedPlane(row(3,0)+row(1,0), row(3,1)+row(1,1), row(3,2)+row(1,2), row(3,3)+row(1,3));
    // top:    w - y >= 0
    f.planes[3] = normalizedPlane(row(3,0)-row(1,0), row(3,1)-row(1,1), row(3,2)-row(1,2), row(3,3)-row(1,3));
    // near:   w + z >= 0   (OpenGL clip space, z in [-w, w])
    f.planes[4] = normalizedPlane(row(3,0)+row(2,0), row(3,1)+row(2,1), row(3,2)+row(2,2), row(3,3)+row(2,3));
    // far:    w - z >= 0
    f.planes[5] = normalizedPlane(row(3,0)-row(2,0), row(3,1)-row(2,1), row(3,2)-row(2,2), row(3,3)-row(2,3));
    return f;
}

bool aabbVisible(const Frustum& f, const Aabb& box) {
    // Positive-vertex (p-vertex) test: for each inward plane, pick the box
    // corner furthest along the plane normal. If even that corner is on the
    // outside (negative) side, the whole box is outside -> cull. Otherwise it
    // is at least partially inside every plane -> keep (conservative).
    for (const Plane& pl : f.planes) {
        Vec3 p{
            pl.n.x >= 0.0f ? box.max.x : box.min.x,
            pl.n.y >= 0.0f ? box.max.y : box.min.y,
            pl.n.z >= 0.0f ? box.max.z : box.min.z,
        };
        if (pl.signedDistance(p) < 0.0f) return false;
    }
    return true;
}

} // namespace wf
