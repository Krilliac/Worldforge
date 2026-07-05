#pragma once
// ---------------------------------------------------------------------------
// scene: compose a world from terrain + placed model instances (M2 doodads and
// WMOs) + a debug overlay, and render the whole thing through one camera. This
// is the "render all the objects" layer -- each instance carries its own posed
// TexMesh, BLP texture, and placement transform (from MDDF/MODF via coords.hpp).
// Backend-agnostic data; renderScene draws it with the software rasteriser (a
// GPU backend would iterate the same instance list).
// ---------------------------------------------------------------------------
#include <vector>

#include "image.hpp"
#include "math.hpp"
#include "terrain.hpp"     // Mesh
#include "raster.hpp"      // Framebuffer, TexMesh
#include "debugdraw.hpp"
#include "world_lod.hpp"   // DrawDistances / doodadAlpha / wmoAlpha

namespace wf {

struct ModelInstance {
    const TexMesh* mesh    = nullptr;   // posed/static, model-local space
    const Image*   texture = nullptr;   // BLP-decoded RGBA
    Mat4           transform = Mat4::identity();   // local -> world placement
    bool           isWmo   = false;     // WMO (long draw distance) vs M2 doodad

    // Translucent-batch state (M2 blend modes 2..6, resolveM2Material). A
    // translucent instance is deferred past every opaque draw and rendered
    // alpha-blended with depth-test ON but depth-write OFF, ordered by
    // (priorityPlane ascending, then view depth FARTHEST first) so overlapping
    // translucency composites identically regardless of submission order.
    bool translucent   = false;
    int  priorityPlane = 0;             // from M2Batch::priorityPlane (int8)

    // Optional animated UV transform (M2 texture transform, e.g. flowing
    // water); sampled per frame via sampleM2TextureTransform and applied in the
    // rasteriser's texel fetch when useUvTransform is set.
    bool useUvTransform = false;
    Mat4 uvTransform = Mat4::identity();
};

// A translucent liquid (MCLQ) surface in world space: the flat water/lava mesh
// plus its tint (alpha in tint.a) and whether it glows (magma/slime). Drawn
// after opaque terrain + instances so the alpha-blend reads the right backdrop.
struct LiquidInstance {
    const Mesh* mesh     = nullptr;     // world-space surface (buildLiquidMesh)
    Rgba        tint{ 40, 110, 180, 140 };
    bool        emissive = false;
};

struct Scene {
    const Mesh*                terrain = nullptr;   // optional untextured terrain
    std::vector<ModelInstance> instances;
    std::vector<LiquidInstance> liquids;            // translucent liquid surfaces
    const DebugDraw*           debug   = nullptr;   // optional overlay
    Vec3 lightDir{ 0.5f, 0.4f, 0.8f };
    // Zone lighting (Light.dbc) ambient/diffuse colours; defaults reproduce the
    // legacy grey light for opaque (0.4/0.6) and liquid (0.5/0.5 sheen) passes.
    // Populate from a resolved LightingSample to apply zone-appropriate light;
    // leave default for the original fixed-light behaviour.
    ShadeLight light;                                     // opaque terrain/objects
    ShadeLight liquidLight{ {0.5f,0.4f,0.8f}, {0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f} };

    // Object draw-distance culling (T3.4). When cullObjects is set, an instance
    // past its kind's cull distance (doodad vs WMO, via DrawDistances) from
    // cameraPos is skipped -- far clutter isn't drawn. Off by default, so the
    // legacy behaviour (draw every instance) is unchanged. Soft alpha fade near
    // the cull edge is a later polish (needs a per-draw alpha multiplier in the
    // rasteriser); the fade math (doodadAlpha/wmoAlpha) already drives the
    // cull threshold.
    bool          cullObjects = false;
    Vec3          cameraPos{ 0, 0, 0 };
    DrawDistances drawDist{};
};

// Render the scene into `fb` through `viewProj`: terrain first (lit/shaded),
// then every opaque textured instance (placement baked via viewProj *
// transform), then the translucent instances sorted by (priorityPlane asc,
// centroid depth farthest-first) with depth-write off, then liquids, then the
// debug overlay on top.
void renderScene(Framebuffer& fb, const Scene& scene, const Mat4& viewProj);

} // namespace wf
