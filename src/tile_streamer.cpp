#include "tile_streamer.hpp"

#include <algorithm>

namespace wf {

// Map tiles are addressed in [0,63] on both axes (a full 64x64 WDT grid).
static constexpr int kMaxTileIndex = 63;

TileStreamer::TileStreamer(int radiusTiles) { setRadius(radiusTiles); }

void TileStreamer::setRadius(int r) { radius_ = (r < 0) ? 0 : r; }

int TileStreamer::radius() const { return radius_; }

std::vector<TileCoord> TileStreamer::desired(TileCoord focus) const {
    std::vector<TileCoord> out;
    // Clamp the disk's extent to the map so we never emit off-map tiles; near a
    // corner the disk is simply truncated (e.g. radius 1 at (0,0) -> 4 tiles).
    const int x0 = std::max(0, focus.x - radius_);
    const int x1 = std::min(kMaxTileIndex, focus.x + radius_);
    const int y0 = std::max(0, focus.y - radius_);
    const int y1 = std::min(kMaxTileIndex, focus.y + radius_);
    if (x1 < x0 || y1 < y0) return out;   // focus fully off-map -> nothing
    out.reserve(static_cast<size_t>(x1 - x0 + 1) * (y1 - y0 + 1));
    // y-major then x: matches TileCoord::operator< for deterministic order.
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            out.push_back(TileCoord{x, y});
    return out;
}

TileStreamer::Plan TileStreamer::plan(TileCoord focus) const {
    Plan p;
    const std::vector<TileCoord> want = desired(focus);
    // toLoad = desired - resident (preserve desired's deterministic order).
    for (const TileCoord& c : want)
        if (resident_.find(c) == resident_.end()) p.toLoad.push_back(c);
    // toEvict = resident - desired. Build a sorted view of `want` for a fast
    // membership test; resident_ already iterates in sorted order.
    std::set<TileCoord> wantSet(want.begin(), want.end());
    for (const TileCoord& c : resident_)
        if (wantSet.find(c) == wantSet.end()) p.toEvict.push_back(c);
    return p;
}

void TileStreamer::markLoaded(TileCoord c)  { resident_.insert(c); }
void TileStreamer::markEvicted(TileCoord c) { resident_.erase(c); }

bool TileStreamer::isResident(TileCoord c) const {
    return resident_.find(c) != resident_.end();
}

size_t TileStreamer::residentCount() const { return resident_.size(); }

std::vector<TileCoord> TileStreamer::resident() const {
    return std::vector<TileCoord>(resident_.begin(), resident_.end());
}

} // namespace wf
