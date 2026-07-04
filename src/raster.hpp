#pragma once
// ---------------------------------------------------------------------------
// Tiny software rasterizer: z-buffered triangle fill with barycentric,
// perspective-correct attribute interpolation. Exists to prove the whole
// data->image pipeline (parse -> mesh -> world transform -> camera -> pixels)
// end to end with no GPU. Not a hot path; clarity over speed.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#include "image.hpp"
#include "math.hpp"
#include "terrain.hpp"   // Mesh / Vertex
#include "debugdraw.hpp" // DebugDraw overlay

namespace wf {

struct Framebuffer {
    Image color;
    std::vector<float> depth;   // NDC z, smaller = nearer; cleared to +inf

    Framebuffer(int w, int h) : color(w, h), depth(static_cast<size_t>(w) * h) {}

    void clear(Rgba bg) {
        for (Rgba& p : color.pixels) p = bg;
        for (float& d : depth) d = std::numeric_limits<float>::infinity();
    }
};

// Per-channel directional shading for the software path. The legacy fixed light
// was a scalar `ambient + diffuse * max(0, dot(n,L))` applied equally to RGB;
// zone lighting (Light.dbc) instead supplies coloured ambient + diffuse terms.
// The default reproduces the legacy grey light exactly (ambient 0.4, diffuse
// 0.6 per channel) so callers that pass nothing are unchanged.
struct ShadeLight {
    Vec3 dir{ 0.5f, 0.4f, 0.8f };          // directional light vector (world)
    Vec3 ambient{ 0.4f, 0.4f, 0.4f };      // ambient colour (added)
    Vec3 diffuse{ 0.6f, 0.6f, 0.6f };      // diffuse colour (× N·L)

    // light contribution per channel for a surface normal n (already normalised
    // L is taken from dir). Returns a per-channel multiplier in [0, ambient+diffuse].
    Vec3 shade(const Vec3& n, const Vec3& Lnorm) const {
        float d = std::max(0.0f, dot(n, Lnorm));
        return { ambient.x + diffuse.x * d,
                 ambient.y + diffuse.y * d,
                 ambient.z + diffuse.z * d };
    }
};

// Screen-space vertex: pixel x/y plus NDC depth.
struct ScreenVert { float x, y, z; };

// Fill a solid triangle with depth test (used by tests; deterministic).
void fillTriangleSolid(Framebuffer& fb, const ScreenVert& a, const ScreenVert& b,
                       const ScreenVert& c, Rgba color);

// Render a world-space mesh through `mvp`, lit by a directional light. Terrain
// is shaded by slope + height so structure is visible without textures.
void rasterMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp, Vec3 lightDir);
// Coloured-light overload: ambient/diffuse come from zone lighting (Light.dbc).
void rasterMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp, const ShadeLight& light);

// A textured mesh vertex: position + normal + a texture coordinate.
struct TexVertex { Vec3 position; Vec3 normal; Vec2 uv; };
struct TexMesh   { std::vector<TexVertex> vertices; std::vector<uint32_t> indices; };

// Nearest-sample a texture with UV repeat (wrap). Shared by the model and
// terrain texturing paths.
Rgba sampleTextureWrap(const Image& tex, float u, float v);

// Render a textured mesh: perspective-correct UV interpolation, nearest texel
// sampling (UV wraps/repeats), modulated by directional + ambient lighting.
// Texels with alpha < 8 are discarded (alpha-test) so cutout textures (foliage)
// read correctly.
// With alphaBlend, texels are composited over the framebuffer by their alpha and
// do NOT write depth (translucent surfaces, e.g. WMO blend-mode >= 2); otherwise
// it is the opaque alpha-tested path.
void rasterTexMesh(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                   const Image& texture, Vec3 lightDir, bool alphaBlend = false);
// Coloured-light overload. `alphaMul` (default 1) scales the composite alpha in
// the blended path -- used for whole-object distance fade (draw an instance at,
// e.g., 0.4 opacity near its cull edge). It only takes effect when alphaBlend is
// true; at alphaMul 1 the behaviour is unchanged.
void rasterTexMesh(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                   const Image& texture, const ShadeLight& light, bool alphaBlend = false,
                   float alphaMul = 1.0f);

// Render an untextured mesh as a single flat-tinted, alpha-blended surface --
// the translucent liquid pass (ADT MCLQ water/ocean/magma/slime). `tint` is the
// liquid colour with its own alpha (tint.a < 255 -> see-through water). Like the
// blended texture path it composites over the framebuffer and does NOT write the
// depth buffer, so the surface reads as glass over the terrain beneath while
// still being occluded by nearer opaque geometry (depth-tested, not -written).
// With `emissive` the tint is used as-is (lava/slime glow); otherwise it is
// modulated by the directional light like opaque terrain. Draw this AFTER the
// opaque terrain + objects so blending composites correctly.
void rasterLiquidMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp,
                      Rgba tint, Vec3 lightDir, bool emissive = false);
// Coloured-light overload: the diffuse sheen uses the zone diffuse colour.
void rasterLiquidMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp,
                      Rgba tint, const ShadeLight& light, bool emissive = false);

// Atmosphere passes (zone sky + distance haze), shared by the offline renders
// and the interactive viewport so both produce the same backdrop.
//
// fillSkyGradient replaces Framebuffer::clear() when a zone sky is resolved:
// a vertical top->horizon gradient with the depth buffer reset to +inf.
void fillSkyGradient(Framebuffer& fb, Rgba top, Rgba horizon);
// applyDistanceFog is a post-pass over the rendered geometry: opaque pixels are
// blended toward `fog` by their normalised scene depth (nearest drawn pixel ->
// 0, farthest -> maxFog). Sky pixels (depth == +inf) are untouched. Run it
// after the geometry passes and before overlay drawing so debug lines stay
// crisp. A frame with no depth range (empty scene) is a no-op.
void applyDistanceFog(Framebuffer& fb, Rgba fog, float maxFog = 0.65f);

struct DebugDrawOptions {
    bool depthTest  = true;    // overlay respects the z-buffer (hidden by terrain)
    bool writeDepth = false;   // overlay doesn't occlude later overlay primitives
    int  pointSize  = 3;       // marker square size in pixels (odd looks centred)
};

// Draw a DebugDraw overlay (lines, translucent triangles, point markers) through
// `mvp`, for every enabled category. Lines/points are projected and clipped at
// the near plane; triangles are alpha-blended over the colour target.
void rasterDebug(Framebuffer& fb, const DebugDraw& dd, const Mat4& mvp,
                 const DebugDrawOptions& opt = {});

} // namespace wf
