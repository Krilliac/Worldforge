#include "debugdraw.hpp"

#include <cmath>
#include <set>
#include <utility>

namespace wf {

void DebugDraw::line(const Vec3& a, const Vec3& b, Rgba c, DebugCategory cat) {
    Buffers& buf = bucket(cat);
    buf.lines.push_back({a, c});
    buf.lines.push_back({b, c});
}

void DebugDraw::triangle(const Vec3& a, const Vec3& b, const Vec3& c, Rgba col, DebugCategory cat) {
    Buffers& buf = bucket(cat);
    buf.tris.push_back({a, col});
    buf.tris.push_back({b, col});
    buf.tris.push_back({c, col});
}

void DebugDraw::point(const Vec3& p, Rgba c, DebugCategory cat) {
    bucket(cat).points.push_back({p, c});
}

void DebugDraw::aabb(const Vec3& mn, const Vec3& mx, Rgba c, DebugCategory cat) {
    const Vec3 v[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    static const int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},   // bottom
        {4,5},{5,6},{6,7},{7,4},   // top
        {0,4},{1,5},{2,6},{3,7},   // verticals
    };
    for (auto& seg : e) line(v[seg[0]], v[seg[1]], c, cat);
}

void DebugDraw::box(const Vec3& center, const Vec3& half, const Quat& rot, Rgba c, DebugCategory cat) {
    Mat4 r = rot.toMat4();
    auto corner = [&](float sx, float sy, float sz) {
        Vec4 p = r * Vec4(half.x * sx, half.y * sy, half.z * sz, 0.0f);
        return Vec3{center.x + p.x, center.y + p.y, center.z + p.z};
    };
    const Vec3 v[8] = {
        corner(-1,-1,-1), corner(+1,-1,-1), corner(+1,+1,-1), corner(-1,+1,-1),
        corner(-1,-1,+1), corner(+1,-1,+1), corner(+1,+1,+1), corner(-1,+1,+1),
    };
    static const int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7},
    };
    for (auto& seg : e) line(v[seg[0]], v[seg[1]], c, cat);
}

void DebugDraw::circle(const Vec3& center, const Vec3& normalIn, float radius, Rgba c,
                       DebugCategory cat, int segments) {
    if (segments < 3) segments = 3;
    Vec3 n = normalize(normalIn);
    // Build two axes spanning the circle's plane.
    Vec3 ref = (std::fabs(n.z) < 0.9f) ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    Vec3 u = normalize(wf::cross(ref, n));
    Vec3 v = wf::cross(n, u);
    Vec3 prev;
    for (int i = 0; i <= segments; ++i) {
        float a = static_cast<float>(2.0 * kPi * i / segments);
        Vec3 p = center + u * (radius * std::cos(a)) + v * (radius * std::sin(a));
        if (i > 0) line(prev, p, c, cat);
        prev = p;
    }
}

void DebugDraw::sphere(const Vec3& center, float radius, Rgba c, DebugCategory cat, int segments) {
    // Three orthogonal great circles -- enough to read a sphere as a wireframe.
    circle(center, {1, 0, 0}, radius, c, cat, segments);
    circle(center, {0, 1, 0}, radius, c, cat, segments);
    circle(center, {0, 0, 1}, radius, c, cat, segments);
}

void DebugDraw::cross(const Vec3& p, float size, Rgba c, DebugCategory cat) {
    const float h = size * 0.5f;
    line({p.x - h, p.y, p.z}, {p.x + h, p.y, p.z}, c, cat);
    line({p.x, p.y - h, p.z}, {p.x, p.y + h, p.z}, c, cat);
    line({p.x, p.y, p.z - h}, {p.x, p.y, p.z + h}, c, cat);
}

void DebugDraw::arrow(const Vec3& from, const Vec3& to, Rgba c, DebugCategory cat, float headSize) {
    line(from, to, c, cat);
    Vec3 dir = to - from;
    float len = length(dir);
    if (len < 1e-5f) return;
    dir = dir * (1.0f / len);
    float hs = (headSize > 0.0f) ? headSize : len * 0.15f;
    // A perpendicular in a stable plane.
    Vec3 ref = (std::fabs(dir.z) < 0.9f) ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    Vec3 perp = normalize(wf::cross(dir, ref));
    Vec3 back = to - dir * hs;
    line(to, back + perp * hs, c, cat);
    line(to, back - perp * hs, c, cat);
}

void DebugDraw::path(const std::vector<Vec3>& pts, Rgba c, DebugCategory cat, bool markers) {
    for (size_t i = 0; i + 1 < pts.size(); ++i) line(pts[i], pts[i + 1], c, cat);
    if (markers)
        for (const Vec3& p : pts) cross(p, 1.0f, c, cat);
}

void DebugDraw::grid(const Vec3& center, float extent, float step, Rgba minor, Rgba major,
                     float majorEvery) {
    if (step <= 0.0f) return;
    int n = static_cast<int>(extent / step);
    for (int i = -n; i <= n; ++i) {
        const float off = i * step;
        const bool isMajor = (majorEvery > 0.0f) && (i % static_cast<int>(majorEvery) == 0);
        Rgba c = isMajor ? major : minor;
        // Lines parallel to X (vary Y) and parallel to Y (vary X), on the z plane.
        line({center.x - extent, center.y + off, center.z},
             {center.x + extent, center.y + off, center.z}, c, DebugCategory::Grid);
        line({center.x + off, center.y - extent, center.z},
             {center.x + off, center.y + extent, center.z}, c, DebugCategory::Grid);
    }
}

void DebugDraw::wireframe(const Mesh& mesh, Rgba c, DebugCategory cat) {
    std::set<std::pair<uint32_t, uint32_t>> edges;
    auto addEdge = [&](uint32_t a, uint32_t b) {
        edges.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    };
    for (size_t i = 0; i + 2 < mesh.indices.size() + 0 && i + 2 < mesh.indices.size(); i += 3) {
        uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], d = mesh.indices[i + 2];
        addEdge(a, b); addEdge(b, d); addEdge(d, a);
    }
    for (const auto& e : edges)
        line(mesh.vertices[e.first].position, mesh.vertices[e.second].position, c, cat);
}

void DebugDraw::normals(const Mesh& mesh, float length, Rgba c, DebugCategory cat) {
    for (const Vertex& v : mesh.vertices)
        line(v.position, v.position + v.normal * length, c, cat);
}

void DebugDraw::frustum(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
                        double fovYDeg, double aspect, double nearD, double farD, Rgba c,
                        DebugCategory cat) {
    const float th = static_cast<float>(std::tan(radians(fovYDeg) * 0.5));
    auto corner = [&](double dist, float sx, float sy) {
        const float h = static_cast<float>(dist) * th;
        const float w = h * static_cast<float>(aspect);
        return eye + forward * static_cast<float>(dist) + right * (w * sx) + up * (h * sy);
    };
    Vec3 n[4] = { corner(nearD,-1,-1), corner(nearD,+1,-1), corner(nearD,+1,+1), corner(nearD,-1,+1) };
    Vec3 f[4] = { corner(farD, -1,-1), corner(farD, +1,-1), corner(farD, +1,+1), corner(farD, -1,+1) };
    for (int i = 0; i < 4; ++i) {
        line(n[i], n[(i + 1) % 4], c, cat);   // near rect
        line(f[i], f[(i + 1) % 4], c, cat);   // far rect
        line(n[i], f[i], c, cat);             // connecting edge
    }
}

void DebugDraw::clear() {
    for (Buffers& b : cat_) { b.lines.clear(); b.tris.clear(); b.points.clear(); }
}

DebugDraw::Stats DebugDraw::stats() const {
    Stats s;
    for (size_t i = 0; i < cat_.size(); ++i) {
        if (!enabled_[i]) continue;
        s.lines     += static_cast<int>(cat_[i].lines.size()  / 2);
        s.triangles += static_cast<int>(cat_[i].tris.size()   / 3);
        s.points    += static_cast<int>(cat_[i].points.size());
    }
    return s;
}

} // namespace wf
