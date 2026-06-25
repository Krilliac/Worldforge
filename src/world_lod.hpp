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

} // namespace wf
