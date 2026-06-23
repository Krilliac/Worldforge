#pragma once
// ---------------------------------------------------------------------------
// Tiny software rasterizer: z-buffered triangle fill with barycentric,
// perspective-correct attribute interpolation. Exists to prove the whole
// data->image pipeline (parse -> mesh -> world transform -> camera -> pixels)
// end to end with no GPU. Not a hot path; clarity over speed.
// ---------------------------------------------------------------------------
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

// Screen-space vertex: pixel x/y plus NDC depth.
struct ScreenVert { float x, y, z; };

// Fill a solid triangle with depth test (used by tests; deterministic).
void fillTriangleSolid(Framebuffer& fb, const ScreenVert& a, const ScreenVert& b,
                       const ScreenVert& c, Rgba color);

// Render a world-space mesh through `mvp`, lit by a directional light. Terrain
// is shaded by slope + height so structure is visible without textures.
void rasterMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp, Vec3 lightDir);

// A textured mesh vertex: position + normal + a texture coordinate.
struct TexVertex { Vec3 position; Vec3 normal; Vec2 uv; };
struct TexMesh   { std::vector<TexVertex> vertices; std::vector<uint32_t> indices; };

// Render a textured mesh: perspective-correct UV interpolation, nearest texel
// sampling (UV wraps/repeats), modulated by directional + ambient lighting.
// Texels with alpha < 8 are discarded (alpha-test) so cutout textures (foliage)
// read correctly.
void rasterTexMesh(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                   const Image& texture, Vec3 lightDir);

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
