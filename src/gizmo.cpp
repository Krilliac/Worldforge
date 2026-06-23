#include "gizmo.hpp"

#include <cmath>
#include <limits>

namespace wf {

Ray cameraRay(const Vec3& eye, const Vec3& forward, const Vec3& right,
              const Vec3& up, double fovYDeg, double aspect, float ndcX, float ndcY) {
    const float th = static_cast<float>(std::tan(radians(fovYDeg) * 0.5));
    // Point on the near image plane one unit ahead of the camera.
    Vec3 dir = normalize(forward
                         + right * (ndcX * th * static_cast<float>(aspect))
                         + up    * (ndcY * th));
    return { eye, dir };
}

bool rayPlane(const Ray& r, const Vec3& p0, const Vec3& n, float& tOut) {
    const float denom = dot(r.dir, n);
    if (std::fabs(denom) < 1e-8f) return false;       // parallel
    const float t = dot(p0 - r.origin, n) / denom;
    if (t < 0.0f) return false;                       // behind the ray
    tOut = t;
    return true;
}

bool rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c, float& tOut) {
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p  = cross(r.dir, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-8f) return false;         // ray parallel to triangle
    const float inv = 1.0f / det;
    const Vec3 tvec = r.origin - a;
    const float u = dot(tvec, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = cross(tvec, e1);
    const float v = dot(r.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = dot(e2, q) * inv;
    if (t < 0.0f) return false;
    tOut = t;
    return true;
}

bool pickMesh(const Ray& r, const Mesh& mesh, Vec3& hitOut) {
    float best = std::numeric_limits<float>::max();
    bool found = false;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.vertices[mesh.indices[i]].position;
        const Vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        float t;
        if (rayTriangle(r, a, b, c, t) && t < best) {
            best = t;
            found = true;
        }
    }
    if (found) hitOut = r.origin + r.dir * best;
    return found;
}

float snap(float v, float step) {
    if (step <= 0.0f) return v;
    return std::round(v / step) * step;
}

Vec3 snap(const Vec3& v, float step) {
    return { snap(v.x, step), snap(v.y, step), snap(v.z, step) };
}

Vec3 dragAlongAxis(const Vec3& pos, const Vec3& axisIn, const Ray& from, const Ray& to) {
    const Vec3 axis = normalize(axisIn);
    // Intersect each ray with the plane through `pos` most perpendicular to the
    // axis, then keep only the component of the difference along the axis.
    // Plane normal: the axis works for a screen-facing drag approximation; we
    // use a plane whose normal is the axis crossed with the view-ish dir, but a
    // robust closed form is the projection of each ray onto the axis line.
    auto closestOnAxis = [&](const Ray& r) -> float {
        // Closest point parameter (along axis from pos) between the axis line
        // (pos + s*axis) and the ray (r.origin + t*r.dir).
        const Vec3 w0 = pos - r.origin;
        const float a = 1.0f;                 // dot(axis,axis), axis normalised
        const float bb = dot(axis, r.dir);
        const float c = dot(r.dir, r.dir);
        const float d = dot(axis, w0);
        const float e = dot(r.dir, w0);
        const float denom = a * c - bb * bb;
        if (std::fabs(denom) < 1e-8f) return d;          // near-parallel: fall back
        return (bb * e - c * d) / denom;                 // s along the axis
    };
    const float s0 = closestOnAxis(from);
    const float s1 = closestOnAxis(to);
    return pos + axis * (s1 - s0);
}

} // namespace wf
