#include "scene.hpp"

namespace wf {

void renderScene(Framebuffer& fb, const Scene& scene, const Mat4& viewProj) {
    // Coloured zone light: ambient/diffuse from scene.light, direction from the
    // (possibly separately-set) lightDir. Defaults reproduce the legacy grey light.
    ShadeLight sl = scene.light;
    sl.dir = scene.lightDir;

    if (scene.terrain)
        rasterMesh(fb, *scene.terrain, viewProj, sl);

    for (const ModelInstance& inst : scene.instances) {
        if (!inst.mesh || !inst.texture) continue;
        if (scene.cullObjects) {
            // Instance world position = the translation column of its placement.
            Vec3 wp{ inst.transform.at(0, 3), inst.transform.at(1, 3), inst.transform.at(2, 3) };
            float d = length(scene.cameraPos - wp);
            float a = inst.isWmo ? wmoAlpha(d, scene.drawDist) : doodadAlpha(d, scene.drawDist);
            if (a <= 0.0f) continue;   // beyond the kind's draw distance -> skip
        }
        rasterTexMesh(fb, *inst.mesh, viewProj * inst.transform, *inst.texture, sl);
    }

    // Translucent liquid surfaces after the opaque passes. Default sheen is the
    // legacy neutral 0.5/0.5; zone lighting can tint it via Scene::liquidLight.
    ShadeLight liqLight = scene.liquidLight;
    liqLight.dir = scene.lightDir;
    for (const LiquidInstance& liq : scene.liquids) {
        if (!liq.mesh) continue;
        rasterLiquidMesh(fb, *liq.mesh, viewProj, liq.tint, liqLight, liq.emissive);
    }

    if (scene.debug) {
        DebugDrawOptions opt; opt.depthTest = true;
        rasterDebug(fb, *scene.debug, viewProj, opt);
    }
}

} // namespace wf
