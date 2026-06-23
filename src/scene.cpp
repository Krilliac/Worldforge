#include "scene.hpp"

namespace wf {

void renderScene(Framebuffer& fb, const Scene& scene, const Mat4& viewProj) {
    if (scene.terrain)
        rasterMesh(fb, *scene.terrain, viewProj, scene.lightDir);

    for (const ModelInstance& inst : scene.instances) {
        if (!inst.mesh || !inst.texture) continue;
        rasterTexMesh(fb, *inst.mesh, viewProj * inst.transform, *inst.texture, scene.lightDir);
    }

    if (scene.debug) {
        DebugDrawOptions opt; opt.depthTest = true;
        rasterDebug(fb, *scene.debug, viewProj, opt);
    }
}

} // namespace wf
