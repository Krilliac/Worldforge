#include "scene_pick.hpp"
#include "picking.hpp"

namespace wf {

ScenePick pickScene(const Ray& r, const TileScene& scene) {
    ScenePick best;
    float bestT = 1e30f;
    auto consider = [&](float t, ScenePick::Kind kind, size_t idx, uint32_t id) {
        if (t >= 0.0f && t < bestT) {
            bestT = t;
            best.kind = kind; best.index = idx; best.uniqueId = id;
            best.distance = t; best.point = r.origin + r.dir * t;
        }
    };

    // Terrain chunk meshes are already in world space (identity placement).
    const Mat4 I = Mat4::identity();
    for (size_t c = 0; c < scene.terrain.chunkMeshes.size(); ++c)
        consider(pickTexMeshXform(r, scene.terrain.chunkMeshes[c], I),
                 ScenePick::Kind::Terrain, c, 0);

    // Placed doodads (textured meshes at their MDDF transform).
    for (size_t i = 0; i < scene.instances.size(); ++i) {
        const TileScene::Inst& in = scene.instances[i];
        if (in.mesh >= scene.meshes.size()) continue;
        consider(pickTexMeshXform(r, scene.meshes[in.mesh], in.transform),
                 ScenePick::Kind::Doodad, i, 0);
    }

    // Placed WMOs (group geometry at their MODF transform), per-triangle.
    for (size_t i = 0; i < scene.wmoInstances.size(); ++i) {
        const TileScene::WmoInst& wi = scene.wmoInstances[i];
        consider(pickMeshXform(r, wi.mesh, wi.transform),
                 ScenePick::Kind::Wmo, i, wi.uniqueId);
    }

    return best;
}

} // namespace wf
