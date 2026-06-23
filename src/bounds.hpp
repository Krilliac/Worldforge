#pragma once
// ---------------------------------------------------------------------------
// Axis-aligned bounding box + model-bounds helpers. The selectable bounds the
// editor needs for tight click-picking: a sphere is enough for a critter, but a
// WMO or a long object wants its real box. M2 bounds are derived from the bind-
// pose vertex pool (no extra header-offset guessing); WMO bounds come straight
// from the root's stored bbox. Pure math over the parsed structs, unit-tested.
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"
#include "m2.hpp"
#include "wmo.hpp"

namespace wf {

struct Aabb {
    Vec3 min{  1e30f,  1e30f,  1e30f };
    Vec3 max{ -1e30f, -1e30f, -1e30f };

    bool  valid()  const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    Vec3  center() const { return (min + max) * 0.5f; }
    Vec3  half()   const { return (max - min) * 0.5f; }      // half-extents
    float radius() const { return valid() ? length(half()) : 0.0f; }  // enclosing sphere

    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
};

// AABB enclosing a point cloud (invalid/empty if `pts` is empty).
Aabb aabbOfPoints(const std::vector<Vec3>& pts);

// Model-local bounds of an M2 (from its bind-pose vertices) and a WMO (root bbox).
Aabb modelBounds(const M2Model& m);
Aabb wmoBounds(const WmoRoot& root);

} // namespace wf
