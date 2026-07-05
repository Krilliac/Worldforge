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
// the named curves match the editor's "Type" dropdown. Over the normalised
// band position t (0 at the inner edge, 1 at the radius): Flat 1, Linear 1-t,
// Smooth smoothstep S-curve, Gaussian bell, Polynomial 1-t^3 (cubic ease),
// Trigonometric cos(t*pi/2), Quadratic 1-t^2. Values are serialised as ints;
// only APPEND new profiles so saved settings stay stable.
enum class Falloff { Flat, Linear, Smooth, Gaussian, Polynomial, Trigonometric, Quadratic };

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

// Source-model variants of the height brushes: instead of editing a derived
// world-space render mesh, these edit a tile's parsed MCNK height grids (MCVT)
// in place, so the edit lives in the authoritative data model and survives a
// re-mesh / export. blockX/blockY are the tile's WDT indices (same world-XY
// placement convention as buildChunkMesh). Each of a chunk's 145 height samples
// is treated as a world-space point and displaced like its mesh counterpart.
// Vertices shared along a chunk border exist in both chunks' grids and, sharing
// the same world XY, receive the same edit -- so the seam stays closed.
// Returns the number of height samples affected (shared-edge samples count once
// per chunk). Re-mesh the affected tile afterwards to see the change.
int brushRaiseLowerChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                          const Brush& b, float sign);
int brushFlattenChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                       const Brush& b, float targetZ);

// How the flatten tool is allowed to move terrain relative to its target:
// Both converges from both sides, RaiseOnly only lifts vertices below the
// target, LowerOnly only drops vertices above it.
enum class FlattenMode { Both, RaiseOnly, LowerOnly };

// A (possibly tilted) flatten target plane, for ramps and sloped roads. The
// plane passes through `lock`; `orientationDeg` is the up-slope direction in
// the world XY plane (0 = +X north, 90 = +Y west, wraps to [0,360)) and
// `angleDeg` is the tilt from horizontal (clamped to [0,89], 0 = level).
struct FlattenPlane {
    Vec3  lock;                 // world-space point the plane passes through
    float orientationDeg = 0;   // up-slope heading, degrees in the XY plane
    float angleDeg       = 0;   // tilt from horizontal, degrees
};

// Target height of `plane` above world point (worldX, worldY):
//   lock.z + tan(angle) * dot(p.xy - lock.xy, (cos(orientation), sin(orientation)))
float flattenPlaneTarget(const FlattenPlane& plane, float worldX, float worldY);

// Flatten toward an angled plane, gated by `mode`. Vertices move toward the
// per-vertex plane target by strength*weight per call (capped so they never
// overshoot the plane); vertices the mode excludes are left untouched and not
// counted. Chunk variant follows the same shared-border-sample convention as
// brushRaiseLowerChunks above.
int brushFlatten(std::vector<Vertex>& verts, const Brush& b,
                 const FlattenPlane& plane, FlattenMode mode);
int brushFlattenChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                       const Brush& b, const FlattenPlane& plane, FlattenMode mode);

// Blur/smooth: each MCVT sample inside the brush is pulled toward the mean
// height of its grid neighbours (within ~1.5 sample spacings, gathered across
// chunk borders by world-XY proximity) by strength*weight. Border samples that
// are duplicated in adjacent chunks are grouped by world XY and written a
// single shared result, so seams stay closed bit-exactly. Returns the number
// of height samples written (shared-edge samples count once per chunk).
int brushSmoothChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                      const Brush& b);

// Paint coverage into a 64x64 AlphaMap. (u,v) and `radius` are in normalised
// chunk space [0,1]; each covered texel's value is moved toward `target`
// (0..255) by strength*weight. Returns the number of texels affected.
int paintAlpha(AlphaMap& map, float u, float v, float radius,
               float strength, Falloff falloff, uint8_t target);

// paintAlpha with airbrush-style semantics: `hardness` in [0,1] is the
// fraction of the radius painted at full strength before the falloff begins
// (the brush's inner ratio) and `pressure` in [0,1] is the per-call
// application rate. Each call moves a texel toward `target` by
// pressure*weight -- converging on the target, never overshooting it.
int paintAlpha(AlphaMap& map, float u, float v, float radius,
               float hardness, float pressure, Falloff falloff, uint8_t target);

// Spray/airbrush scatter: place `dabsPerStep` sub-dabs at uniform-random
// offsets inside the disc of `outerRadius` (all in normalised chunk space),
// each dab painted with paintAlpha at `dabRadius`. Randomness comes from a
// tiny explicit xorshift32 PRNG advanced through `rngState`, so a given seed
// reproduces the exact same scatter (0 is remapped to a nonzero seed).
// Returns the total number of texel writes across all dabs.
int sprayAlpha(AlphaMap& map, float u, float v, float outerRadius,
               float dabRadius, int dabsPerStep, uint32_t& rngState,
               float pressure, Falloff falloff, uint8_t target);

// A grayscale image used as a height-brush kernel: intensity/255 multiplies
// the height delta. `pixels` is row-major width*height bytes (row 0 maps to
// v=0); the view is not owned. `rotationDeg` spins the kernel about the brush
// centre in the world XY plane.
struct ImageBrush {
    const uint8_t* pixels = nullptr;
    int   width  = 0;
    int   height = 0;
    float rotationDeg = 0;
};

// Kernel weight in [0,1] at brush-local offset (dx,dy): the offset is rotated
// by -rotationDeg, mapped to image UV over the square [-radius,radius]
// footprint and bilinearly sampled. Returns 0 outside [0,1] UV (and for an
// empty/degenerate brush). A 1x1 image degenerates to a flat brush.
float imageBrushWeight(const ImageBrush& ib, float dx, float dy, float radius);

// Stamp the image kernel onto the source MCVT height grids: like
// brushRaiseLowerChunks but with the weight taken from the image, displacing
// each covered sample by sign * strength * weight. Same shared-border-sample
// convention as brushRaiseLowerChunks. Returns the number of samples affected.
int brushStampChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                     Vec3 center, float radius, float strength, float sign,
                     const ImageBrush& ib);

} // namespace wf
