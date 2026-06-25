#pragma once
// ---------------------------------------------------------------------------
// Liquid surface appearance: the finalized per-type colour/shading model for
// water, ocean, magma and slime surfaces, used by the offline/software raster.
//
// terrain.hpp's liquidTint() returns deliberate PLACEHOLDER tints (the real
// client derives water colours from the Light*Band.dbc colour curves). This
// module is the considered replacement: a documented per-type LiquidSurface
// (shallow vs deep tint, emissive flag, specular/fresnel hints) plus a pure
// shade function liquidShade() that blends shallow->deep by depth and applies
// directional (N.L) shading, matching terrain_render's lighting in spirit.
//
// Numbers are hand-tuned to the vanilla 1.12 look, NOT lifted byte-for-byte
// from a DBC; any field that maps to a real client value is VERIFY-FLAGGED.
// ---------------------------------------------------------------------------
#include "image.hpp"     // Rgba
#include "terrain.hpp"   // LiquidType

namespace wf {

// Per-type surface description. baseTint is the shallow-water colour (over thin
// liquid / at the shoreline); deepTint is the colour over deep liquid; the two
// are linearly blended by a depth factor. Alpha follows the same blend, so deep
// water reads more opaque. specular/fresnel are 0..1 hints for a future glossy
// pass; the software shade path below uses neither yet but keeps them so the GL
// renderer and the raster agree on one source of truth.
struct LiquidSurface {
    Rgba  baseTint;          // shallow / edge colour (RGBA, a = coverage)
    Rgba  deepTint;          // deep-water colour
    bool  emissive = false;  // magma/slime glow: ignore directional shading
    float specular = 0.0f;   // 0..1 highlight strength (water glossy, lava dull)
    float fresnel  = 0.0f;   // 0..1 grazing-angle reflectance boost
};

// Finalized surface appearance for a liquid category. River/Ocean are the
// translucent blues; Magma/Slime are near-opaque and emissive; None is a fully
// transparent surface (nothing to draw). See the .cpp for the per-value notes.
LiquidSurface liquidSurface(LiquidType type);

// Final RGBA of a liquid texel for the software raster.
//   depth01  -- 0 at the shoreline .. 1 over the deepest water; blends
//               baseTint -> deepTint (colour AND alpha).
//   ndotl    -- surface-normal . light direction, clamped to [0,1]; scales the
//               RGB for simple directional shading. IGNORED for emissive liquids
//               (magma/slime keep full brightness on shadowed slopes), matching
//               liquidEmissive() / terrain_render's skip-shading rule.
// None returns a zero/transparent Rgba{0,0,0,0}.
Rgba liquidShade(LiquidType type, float depth01, float ndotl);

} // namespace wf
