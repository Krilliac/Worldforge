#include "world_pick.hpp"

namespace wf {

WorldHit pickWorld(const Ray& ray, const std::vector<PickTile>& tiles) {
    WorldHit out;          // hit=false, dist=0 until something is struck
    for (const PickTile& pt : tiles) {
        if (!pt.mesh || pt.mesh->indices.empty()) continue;   // nothing to test
        Vec3 p;
        if (!pickMesh(ray, *pt.mesh, p)) continue;            // ray missed this tile
        // pickMesh already kept the nearest triangle within the mesh; rank tiles
        // by distance from the ray origin and keep the global closest.
        const float d = length(p - ray.origin);
        if (!out.hit || d < out.dist) {
            out.hit   = true;
            out.tile  = pt.coord;
            out.point = p;
            out.dist  = d;
        }
    }
    return out;
}

} // namespace wf
