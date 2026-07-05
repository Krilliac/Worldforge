#include "scene.hpp"

#include <algorithm>

namespace wf {
namespace {

// View-depth proxy for translucent ordering: the clip-space w of the instance's
// model-local centroid through viewProj * transform. For a perspective
// projection w is the distance along the view axis (larger = farther), which is
// exactly the "batch-centroid view-space depth" the sort key needs without
// requiring the view matrix separately.
float instanceViewDepth(const ModelInstance& inst, const Mat4& viewProj) {
    Vec3 c{0, 0, 0};
    if (inst.mesh && !inst.mesh->vertices.empty()) {
        for (const TexVertex& v : inst.mesh->vertices) c += v.position;
        c = c * (1.0f / static_cast<float>(inst.mesh->vertices.size()));
    }
    Vec4 clip = (viewProj * inst.transform) * Vec4(c, 1.0f);
    return clip.w;
}

} // namespace

void renderScene(Framebuffer& fb, const Scene& scene, const Mat4& viewProj) {
    // Coloured zone light: ambient/diffuse from scene.light, direction from the
    // (possibly separately-set) lightDir. Defaults reproduce the legacy grey light.
    ShadeLight sl = scene.light;
    sl.dir = scene.lightDir;

    if (scene.terrain)
        rasterMesh(fb, *scene.terrain, viewProj, sl);

    // Opaque pass (depth-write on). Translucent instances are deferred past
    // every opaque draw, carrying their distance-fade alpha and centroid depth.
    struct BlendedDraw { const ModelInstance* inst; float fade; float depth; };
    std::vector<BlendedDraw> blended;

    for (const ModelInstance& inst : scene.instances) {
        if (!inst.mesh || !inst.texture) continue;
        float fade = 1.0f;
        if (scene.cullObjects) {
            // Instance world position = the translation column of its placement.
            Vec3 wp{ inst.transform.at(0, 3), inst.transform.at(1, 3), inst.transform.at(2, 3) };
            float d = length(scene.cameraPos - wp);
            fade = inst.isWmo ? wmoAlpha(d, scene.drawDist) : doodadAlpha(d, scene.drawDist);
            if (fade <= 0.0f) continue;                  // beyond draw distance -> skip
        }
        if (inst.translucent) {
            blended.push_back({ &inst, fade, instanceViewDepth(inst, viewProj) });
            continue;
        }
        TexDrawOptions opt;
        opt.useUvTransform = inst.useUvTransform;
        opt.uvTransform    = inst.uvTransform;
        if (fade < 1.0f) {                               // fade band -> soft fade
            opt.alphaBlend = true;
            opt.alphaMul   = fade;
            opt.depthWrite = false;
        }
        rasterTexMesh(fb, *inst.mesh, viewProj * inst.transform, *inst.texture, sl, opt);
    }

    // Blended pass: priorityPlane ascending, ties broken by centroid view depth
    // FARTHEST first, depth-tested but never depth-written -- so overlapping
    // translucency composites identically regardless of submission order (the
    // sort contract, verified in test_raster). stable_sort keeps coincident
    // batches (same plane AND depth) in submission order, deterministically.
    std::stable_sort(blended.begin(), blended.end(),
        [](const BlendedDraw& a, const BlendedDraw& b) {
            if (a.inst->priorityPlane != b.inst->priorityPlane)
                return a.inst->priorityPlane < b.inst->priorityPlane;
            return a.depth > b.depth;
        });
    for (const BlendedDraw& bd : blended) {
        const ModelInstance& inst = *bd.inst;
        TexDrawOptions opt;
        opt.alphaBlend     = true;
        opt.alphaMul       = bd.fade;
        opt.depthWrite     = false;
        opt.useUvTransform = inst.useUvTransform;
        opt.uvTransform    = inst.uvTransform;
        rasterTexMesh(fb, *inst.mesh, viewProj * inst.transform, *inst.texture, sl, opt);
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
