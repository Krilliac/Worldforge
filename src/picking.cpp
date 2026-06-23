#include "picking.hpp"

#include <algorithm>
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

float rayObb(const Ray& r, const Vec3& pos, float yaw, const Vec3& localMin, const Vec3& localMax) {
    // Transform the ray into the box's local frame (un-yaw about +Z, untranslate).
    const float c = std::cos(yaw), s = std::sin(yaw);
    Vec3 d = r.origin - pos;
    Vec3 o{  d.x * c + d.y * s, -d.x * s + d.y * c, d.z };          // Rz(-yaw) * d
    Vec3 dir{ r.dir.x * c + r.dir.y * s, -r.dir.x * s + r.dir.y * c, r.dir.z };

    float tmin = -1e30f, tmax = 1e30f;
    const float lo[3] = { localMin.x, localMin.y, localMin.z };
    const float hi[3] = { localMax.x, localMax.y, localMax.z };
    const float oo[3] = { o.x, o.y, o.z };
    const float dd[3] = { dir.x, dir.y, dir.z };
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-8f) {
            if (oo[i] < lo[i] || oo[i] > hi[i]) return -1.0f;      // parallel & outside
        } else {
            float inv = 1.0f / dd[i];
            float t1 = (lo[i] - oo[i]) * inv, t2 = (hi[i] - oo[i]) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1.0f;
        }
    }
    if (tmax < 0.0f) return -1.0f;                                 // box behind the ray
    return tmin >= 0.0f ? tmin : tmax;                            // 0 if origin inside
}

float pickMeshXform(const Ray& r, const Mesh& mesh, const Mat4& xform) {
    auto xf = [&](const Vec3& p) {
        Vec4 w = xform * Vec4{ p.x, p.y, p.z, 1.0f };
        return Vec3{ w.x, w.y, w.z };
    };
    float best = -1.0f;
    const auto& v = mesh.vertices;
    const auto& idx = mesh.indices;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        float t;
        if (rayTriangle(r, xf(v[idx[i]].position), xf(v[idx[i+1]].position),
                        xf(v[idx[i+2]].position), t) && (best < 0.0f || t < best))
            best = t;
    }
    return best;
}

float pickTexMeshXform(const Ray& r, const TexMesh& mesh, const Mat4& xform) {
    auto xf = [&](const Vec3& p) {
        Vec4 w = xform * Vec4{ p.x, p.y, p.z, 1.0f };
        return Vec3{ w.x, w.y, w.z };
    };
    float best = -1.0f;
    const auto& v = mesh.vertices;
    const auto& idx = mesh.indices;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        float t;
        if (rayTriangle(r, xf(v[idx[i]].position), xf(v[idx[i+1]].position),
                        xf(v[idx[i+2]].position), t) && (best < 0.0f || t < best))
            best = t;
    }
    return best;
}

PickResult pickEntity(const Ray& r, const WorldView& view, float pad, float fallback) {
    PickResult best;
    float bestT = 1e30f;
    for (const LiveEntity& le : view.entities()) {
        const EntityState& s = le.state;
        // Use the real model box when present (tight WMO/long-object picking),
        // otherwise the bounding sphere.
        float t = s.hasBox()
                    ? rayObb(r, s.pos, s.orientation, s.aabbMin, s.aabbMax)
                    : raySphere(r, s.pos, pickRadius(s, pad, fallback));
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
