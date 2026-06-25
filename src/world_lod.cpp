// ---------------------------------------------------------------------------
// Per-tile LOD selection: distance banding against the tile centre. See
// world_lod.hpp for the band semantics.
// ---------------------------------------------------------------------------
#include "world_lod.hpp"

namespace wf {

float tileDistance(Vec3 camera, TileCoord t) {
    // length() is full 3D; tileCenterWorld z is 0, so this folds in the
    // camera's height above the (flat) tile-centre reference plane.
    return length(camera - tileCenterWorld(t.x, t.y));
}

TileLod selectLod(Vec3 camera, TileCoord t, const LodThresholds& th) {
    float d = tileDistance(camera, t);
    if (d <= th.fullDist) return TileLod::Full;
    if (d <= th.farDist)  return TileLod::Far;
    return TileLod::Culled;
}

std::vector<TileLodResult> classify(Vec3 camera,
                                    const std::vector<TileCoord>& tiles,
                                    const LodThresholds& th) {
    std::vector<TileLodResult> out;
    out.reserve(tiles.size());
    for (const TileCoord& t : tiles) {
        // Reuse the same distance for both the band test and the reported value
        // so callers can sort/debug without recomputing.
        float d = tileDistance(camera, t);
        TileLod lod = (d <= th.fullDist) ? TileLod::Full
                    : (d <= th.farDist)  ? TileLod::Far
                                         : TileLod::Culled;
        out.push_back({ t, lod, d });
    }
    return out;
}

} // namespace wf
