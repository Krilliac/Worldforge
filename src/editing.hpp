#pragma once
// ---------------------------------------------------------------------------
// Editor tool core: brush falloff + terrain-height and alpha-coverage brushes.
// This is the GPU-independent logic behind the editor's sculpt / flatten /
// texture-paint tools (the ImGui panels and GL viewport sit on top of it). It
// operates directly on the parsed data model -- world-space terrain vertices
// (terrain.hpp Vertex) and 64x64 AlphaMaps -- so it is fully unit-testable
// headless, in the same spirit as the software rasteriser.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "math.hpp"
#include "terrain.hpp"

namespace wf {

// Brush falloff profiles. Weight is 1.0 at the centre and 0.0 at/after radius;
// the named curves match the editor's "Type" dropdown (Noggit's Flat/Linear/
// Smooth/Gaussian).
enum class Falloff { Flat, Linear, Smooth, Gaussian };

// Falloff weight in [0,1] for a sample `dist` from the brush centre. `radius`
// > 0; samples at or beyond it return 0. `innerRatio` in [0,1] is the fraction
// of the radius held at full strength before the curve begins.
float falloffWeight(Falloff f, float dist, float radius, float innerRatio = 0.0f);

struct Brush {
    Vec3    center;                       // world space (XY used for terrain)
    float   radius     = 10.0f;
    float   strength   = 1.0f;            // yards/stroke (height) or 0..1 (alpha)
    float   innerRatio = 0.0f;
    Falloff falloff    = Falloff::Smooth;
};

// Raise (sign +1) or lower (sign -1) terrain vertex heights under the brush.
// Distance is measured in the XY (north/west) plane; .z is displaced by
// sign * strength * weight. Returns the number of vertices affected.
int brushRaiseLower(std::vector<Vertex>& verts, const Brush& b, float sign);

// Pull vertex heights toward targetZ by strength*weight (flatten/level tool).
int brushFlatten(std::vector<Vertex>& verts, const Brush& b, float targetZ);

// Paint coverage into a 64x64 AlphaMap. (u,v) and `radius` are in normalised
// chunk space [0,1]; each covered texel's value is moved toward `target`
// (0..255) by strength*weight. Returns the number of texels affected.
int paintAlpha(AlphaMap& map, float u, float v, float radius,
               float strength, Falloff falloff, uint8_t target);

} // namespace wf
