#include "world_view.hpp"

#include <algorithm>
#include <cmath>

namespace wf {

void WorldView::onFrame(const EditorFrame& f, uint32_t nowMs) {
    switch (f.opcode) {
        case EDITOR_ENTITY_STATE:  apply(decodeEntityState(f.payload), nowMs); break;
        case EDITOR_ENTITY_REMOVE: remove(decodeEntityRemove(f.payload).guid); break;
        case EDITOR_SERVER_STATE:  serverStatus(decodeServerStatus(f.payload)); break;
        default: break;   // not an entity-stream frame
    }
}

void WorldView::apply(const EntityState& e, uint32_t nowMs) {
    LiveEntity& le = ents_[e.guid];
    le.prevPos    = (le.updates == 0) ? e.pos : le.state.pos;
    le.state      = e;
    le.lastSeenMs = nowMs;
    ++le.updates;
}

void WorldView::remove(uint64_t guid) {
    ents_.erase(guid);
    if (selected_ == guid) selected_ = 0;
}

void WorldView::clear() { ents_.clear(); selected_ = 0; }

const LiveEntity* WorldView::find(uint64_t guid) const {
    auto it = ents_.find(guid);
    return it == ents_.end() ? nullptr : &it->second;
}

std::vector<LiveEntity> WorldView::entities() const {
    std::vector<LiveEntity> out;
    out.reserve(ents_.size());
    for (const auto& kv : ents_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const LiveEntity& a, const LiveEntity& b){ return a.state.guid < b.state.guid; });
    return out;
}

WorldView::Counts WorldView::counts() const {
    Counts c;
    for (const auto& kv : ents_) {
        const EntityState& s = kv.second.state;
        ++c.total;
        switch (s.kind) {
            case 0: ++c.creatures;   break;
            case 1: ++c.players;     break;
            case 2: ++c.gameObjects; break;
            default: break;
        }
        if (s.moving) ++c.moving;
    }
    return c;
}

size_t WorldView::prune(uint32_t nowMs, uint32_t maxAgeMs) {
    size_t removed = 0;
    for (auto it = ents_.begin(); it != ents_.end(); ) {
        if (nowMs - it->second.lastSeenMs > maxAgeMs) {
            if (selected_ == it->first) selected_ = 0;
            it = ents_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

Rgba WorldView::kindColor(uint8_t kind) {
    switch (kind) {
        case 0:  return Rgba{ 90, 220, 110, 255 };   // creature  -- green
        case 1:  return Rgba{ 90, 200, 255, 255 };   // player    -- cyan
        case 2:  return Rgba{ 240, 210, 90, 255 };   // gameobject-- amber
        default: return Rgba{ 200, 200, 200, 255 };
    }
}

void WorldView::buildDebug(DebugDraw& dd) const {
    for (const auto& kv : ents_) {
        const EntityState& s = kv.second.state;
        Rgba col = kindColor(s.kind);
        const bool sel = (s.guid == selected_);
        const float size = sel ? 3.5f : 2.0f;

        dd.cross(s.pos, size, col, DebugCategory::Marker);
        dd.point(s.pos, col, DebugCategory::Marker);

        // A short facing arrow in the XY ground plane (orientation = atan2(y,x)).
        Vec3 dir{ std::cos(s.orientation), std::sin(s.orientation), 0.0f };
        dd.arrow(s.pos, s.pos + dir * (size + 1.0f), col, DebugCategory::Marker);

        // Highlight the selection with a ring so it's easy to find.
        if (sel) dd.circle(s.pos, Vec3{0,0,1}, size + 1.5f, Rgba{255,255,255,255},
                           DebugCategory::Marker);
    }
}

} // namespace wf
