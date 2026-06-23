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

} // namespace wf
