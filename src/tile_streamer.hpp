#pragma once
// ---------------------------------------------------------------------------
// Tile streaming POLICY. Given a focus tile (usually the camera's tile), this
// decides which ADT tiles SHOULD be resident -- a Chebyshev (square) disk of
// `radius` tiles around the focus, clamped to the [0,63] map bounds. It owns no
// file I/O: the caller asks for a plan (what to load / what to evict), performs
// the work, and reports back via markLoaded/markEvicted so this stays in sync.
//
// Chebyshev (max-norm) radius is used because tiles form a grid: radius r keeps
// the (2r+1)^2 square block around the focus resident, matching how a square
// view footprint maps onto square tiles.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <set>
#include <vector>

#include "world_types.hpp"

namespace wf {

class TileStreamer {
   public:
    explicit TileStreamer(int radiusTiles = 2);

    void setRadius(int r);   // clamp r >= 0
    int  radius() const;

    // Chebyshev disk of `radius` around `focus`, clamped to [0,63] both axes.
    // Deterministic order (y-major then x), so plans/diffs are reproducible.
    std::vector<TileCoord> desired(TileCoord focus) const;

    struct Plan {
        std::vector<TileCoord> toLoad;    // desired but not yet resident
        std::vector<TileCoord> toEvict;   // resident but no longer desired
    };
    // Diff the desired set against what is resident. Pure: changes nothing.
    Plan plan(TileCoord focus) const;

    void markLoaded(TileCoord c);
    void markEvicted(TileCoord c);

    bool   isResident(TileCoord c) const;
    size_t residentCount() const;
    // Resident tiles in deterministic order (std::set keeps TileCoord sorted).
    std::vector<TileCoord> resident() const;

   private:
    int                radius_ = 2;
    std::set<TileCoord> resident_;   // TileCoord::operator< gives stable order
};

} // namespace wf
