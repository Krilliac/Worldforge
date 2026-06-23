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

} // namespace wf
