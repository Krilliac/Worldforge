#pragma once
// ---------------------------------------------------------------------------
// Per-tile level-of-detail selection for the streaming world. Given the camera
// position, each loaded tile is bucketed into Full / Far / Culled by its 3D
// distance to the tile centre (in yards). The streamer/renderer uses this to
// decide which tiles draw full-res ADT terrain, which fall back to the low-res
// WDL mesh, and which are skipped entirely. Pure math, no GL -- unit-tested.
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"
#include "world_types.hpp"

namespace wf {

// Distance bands (yards, camera-to-tile-centre). Defaults: a tile is 533.33 yds
// across, so ~2 tiles of slack keeps the camera's neighbours full-res, and the
// far band reaches well past the visible horizon before culling.
struct LodThresholds {
    float fullDist = 1100.0f;   // <= this -> Full
    float farDist  = 6000.0f;   // <= this -> Far, else Culled
};

// |camera - tileCenterWorld(t)|, full 3D (includes the camera's height).
float tileDistance(Vec3 camera, TileCoord t);

// Bucket one tile: <=fullDist Full, <=farDist Far, otherwise Culled.
TileLod selectLod(Vec3 camera, TileCoord t, const LodThresholds& th = {});

// A tile paired with its chosen LOD and the distance that produced it.
struct TileLodResult {
    TileCoord tile;
    TileLod   lod = TileLod::Culled;
    float     dist = 0.0f;
};

// Classify a whole tile list; returns exactly one result per input tile.
std::vector<TileLodResult> classify(Vec3 camera,
                                    const std::vector<TileCoord>& tiles,
                                    const LodThresholds& th = {});

// ---- draw-distance fade for placed objects (M2 doodads / WMOs) ---------------
// Terrain streams far (LodThresholds), but doodads and map-objects have their
// own, much shorter draw distances and fade out with distance instead of popping.
// fadeAlpha is the fraction visible at distance `d`: 1 up to `fadeStart`, then a
// linear ramp to 0 at `cullDist` (0 == cull, don't draw). A cullDist <= fadeStart
// degrades to a hard cut at cullDist. Pure; the renderer multiplies object alpha
// by this and skips objects whose alpha is 0.
float fadeAlpha(float d, float fadeStart, float cullDist);

// Per-object-kind draw distances. Doodads (grass, clutter, small props) fade out
// close; WMOs (buildings) are visible much farther. Yards; tunable like the
// vanilla draw-distance CVars. Defaults are conservative mid-range values.
struct DrawDistances {
    float doodadFade = 200.0f;   // doodad fully visible within this
    float doodadCull = 300.0f;   // doodad gone beyond this
    float wmoFade    = 700.0f;
    float wmoCull    = 1000.0f;
};

// Visible fraction of a doodad / WMO at distance `d` (0 == culled).
float doodadAlpha(float d, const DrawDistances& dd = {});
float wmoAlpha(float d, const DrawDistances& dd = {});

// Cross-fade weight of a Full tile vs the coarse WDL horizon at distance `d`:
// 1 (full tile) until `band` yards before `fullDist`, ramping to 0 at `fullDist`
// (beyond which only the WDL far mesh shows). The renderer blends the full tile
// over the WDL by this weight so the LOD switch doesn't pop. This is `fadeAlpha`
// applied at the tile boundary.
float tileFullWeight(float d, float fullDist, float band);

} // namespace wf
