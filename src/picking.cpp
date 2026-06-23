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

float pickRadius(const EntityState& s, float pad, float fallback) {
    return (s.boundingRadius > 0.0f ? s.boundingRadius : fallback) + pad;
}

PickResult pickEntity(const Ray& r, const WorldView& view, float pad, float fallback) {
    PickResult best;
    float bestT = 1e30f;
    for (const LiveEntity& le : view.entities()) {
        float t = raySphere(r, le.state.pos, pickRadius(le.state, pad, fallback));
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
    Vec3 hit;
    if (pickMesh(r, terrain, hit)) {        // nearest triangle hit (gizmo.hpp)
        best.kind = PickResult::Kind::Terrain;
        best.point = hit;
        best.distance = length(hit - r.origin);
    }
    return best;
}

PickResult pick(const Ray& r, const WorldView& view, const Mesh& terrain, float pad, float fallback) {
    PickResult e = pickEntity(r, view, pad, fallback);
    PickResult t = pickTerrain(r, terrain);
    if (e.hit() && t.hit()) return (e.distance <= t.distance) ? e : t;
    return e.hit() ? e : t;
}

} // namespace wf
