#pragma once
// ---------------------------------------------------------------------------
// WorldView: the engine-side mirror of the server's live world. It ingests the
// EDITOR_ENTITY_STATE / EDITOR_ENTITY_REMOVE stream (from BridgeClient::poll)
// and keeps an up-to-date table of every object the server says is visible --
// the "runtime data from the server" the inspector lists and the viewport draws
// as moving markers. It also tracks per-entity update counts + last-seen times
// so stale objects (a dropped link) can be pruned, and a selection for the
// inspector/gizmo. Pure logic (no ImGui), so it is unit-tested headlessly.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "editor_bridge.hpp"   // EntityState / EntityRemove / EditorFrame
#include "debugdraw.hpp"
#include "math.hpp"

namespace wf {

// One tracked object: its last reported state plus bookkeeping for the view.
struct LiveEntity {
    EntityState state;
    uint32_t    lastSeenMs = 0;   // sim/clock time of the last update
    uint32_t    updates    = 0;   // how many state frames we've applied
    Vec3        prevPos;          // position before the last update (for trails)
};

class WorldView {
public:
    // Feed one decoded bridge frame; ignores frames that aren't entity stream
    // messages. `nowMs` stamps the update for staleness pruning.
    void onFrame(const EditorFrame& f, uint32_t nowMs = 0);

    // Apply a state update / removal directly (used by onFrame and by tests).
    void apply(const EntityState& e, uint32_t nowMs = 0);
    void remove(uint64_t guid);
    void clear();

    // ---- queries ----------------------------------------------------------
    size_t count() const { return ents_.size(); }
    const LiveEntity* find(uint64_t guid) const;
    // GUID-sorted snapshot for a stable inspector list.
    std::vector<LiveEntity> entities() const;

    struct Counts { size_t total = 0, creatures = 0, players = 0, gameObjects = 0, moving = 0; };
    Counts counts() const;

    // Drop entities not seen within `maxAgeMs` of `nowMs` (a stalled/lost link).
    size_t prune(uint32_t nowMs, uint32_t maxAgeMs);

    // ---- selection (inspector <-> viewport) -------------------------------
    void     select(uint64_t guid) { selected_ = guid; }
    uint64_t selected() const { return selected_; }
    bool     hasSelection() const { return selected_ != 0 && ents_.count(selected_) != 0; }

    // Emit a marker per entity (coloured by kind, with a facing arrow); the
    // selected entity is highlighted. This is what makes NPCs/players show up
    // moving in the viewport's debug overlay.
    void buildDebug(DebugDraw& dd) const;

    // Colour convention for a kind (0 creature, 1 player, 2 gameobject).
    static Rgba kindColor(uint8_t kind);

private:
    std::unordered_map<uint64_t, LiveEntity> ents_;
    uint64_t selected_ = 0;
};

} // namespace wf
