#include "picking.hpp"

#include <cmath>

namespace wf {

Ray screenRay(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
              double fovYDeg, double aspect, float sx, float sy, float width, float height) {
    const float ndcX = 2.0f * (sx / width) - 1.0f;
    const float ndcY = 1.0f - 2.0f * (sy / height);          // screen y is top-down
    const float tanHalf = static_cast<float>(std::tan(radians(fovYDeg) * 0.5));

    Vec3 dir = forward
             + right * (ndcX * tanHalf * static_cast<float>(aspect))
             + up    * (ndcY * tanHalf);
    return { eye, normalize(dir) };
}

float raySphere(const Ray& r, const Vec3& center, float radius) {
    Vec3  oc = r.origin - center;
    float b  = dot(oc, r.dir);
    float c  = dot(oc, oc) - radius * radius;
    float disc = b * b - c;                                  // dir is unit -> a = 1
    if (disc < 0.0f) return -1.0f;
    float s = std::sqrt(disc);
    float t = -b - s;                                        // near root
    if (t < 0.0f) t = -b + s;                                // inside the sphere
    return t >= 0.0f ? t : -1.0f;
}

float rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c) {
    const float kEps = 1e-7f;
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p  = cross(r.dir, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < kEps) return -1.0f;                 // ray parallel to tri
    float inv = 1.0f / det;
    Vec3 tvec = r.origin - a;
    float u = dot(tvec, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    Vec3 q = cross(tvec, e1);
    float v = dot(r.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    float t = dot(e2, q) * inv;
    return t > kEps ? t : -1.0f;
}

PickResult pickEntity(const Ray& r, const WorldView& view, float radius) {
    PickResult best;
    float bestT = 1e30f;
    for (const LiveEntity& le : view.entities()) {
        float t = raySphere(r, le.state.pos, radius);
        if (t >= 0.0f && t < bestT) {
            bestT = t;
            best.kind = PickResult::Kind::Entity;
            best.guid = le.state.guid;
            best.distance = t;
            best.point = r.origin + r.dir * t;
        }
    }
    return best;
}

PickResult pickTerrain(const Ray& r, const Mesh& terrain) {
    PickResult best;
    float bestT = 1e30f;
    const auto& v = terrain.vertices;
    const auto& idx = terrain.indices;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        float t = rayTriangle(r, v[idx[i]].position, v[idx[i+1]].position, v[idx[i+2]].position);
        if (t >= 0.0f && t < bestT) {
            bestT = t;
            best.kind = PickResult::Kind::Terrain;
            best.distance = t;
            best.point = r.origin + r.dir * t;
        }
    }
    return best;
}

PickResult pick(const Ray& r, const WorldView& view, const Mesh& terrain, float entityRadius) {
    PickResult e = pickEntity(r, view, entityRadius);
    PickResult t = pickTerrain(r, terrain);
    if (e.hit() && t.hit()) return (e.distance <= t.distance) ? e : t;
    return e.hit() ? e : t;
}

} // namespace wf
