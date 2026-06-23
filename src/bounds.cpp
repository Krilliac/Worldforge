#include "bounds.hpp"

namespace wf {

Aabb aabbOfPoints(const std::vector<Vec3>& pts) {
    Aabb b;
    for (const Vec3& p : pts) b.expand(p);
    return b;
}

Aabb modelBounds(const M2Model& m) {
    Aabb b;
    for (const M2Vertex& v : m.vertices) b.expand(v.pos);
    return b;
}

Aabb wmoBounds(const WmoRoot& root) {
    Aabb b;
    b.min = root.bboxMin;
    b.max = root.bboxMax;
    // A default/degenerate bbox (all-zero, no volume) means "no bounds".
    if (b.max.x <= b.min.x && b.max.y <= b.min.y && b.max.z <= b.min.z) return Aabb{};
    return b;
}

Aabb wmoBounds(const WmoModel& wmo) {
    Aabb b;
    for (const WmoGroup& g : wmo.groups)
        for (const Vec3& v : g.vertices) b.expand(v);
    return b.valid() ? b : wmoBounds(wmo.root);   // fall back to the root bbox
}

Mesh wmoPickMesh(const WmoModel& wmo) {
    Mesh mesh;
    for (const WmoGroup& g : wmo.groups) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const Vec3& v : g.vertices) mesh.vertices.push_back(Vertex{ v, Vec3{0,0,1} });
        for (uint16_t idx : g.indices)   mesh.indices.push_back(base + idx);
    }
    return mesh;
}

} // namespace wf
