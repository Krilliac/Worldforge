#pragma once
// ---------------------------------------------------------------------------
// Editor command stack: flag-gated undo/redo built on pre/post chunk
// snapshots and object placement records. An action is opened with begin(),
// accumulates copy-on-write PRE snapshots as the tool touches chunks, and is
// sealed by commit(), which captures the matching POST snapshots. Undo/redo
// then simply re-applies the stored snapshots -- no per-tool inverse code.
//
// The stack is deliberately decoupled from World/editor types: the host hands
// in small callback bundles (CaptureFns/ApplyFns) that read/write its live
// data model, so the whole module is headless and unit-testable with fake
// lambdas over an in-memory map. No ImGui dependency; it lives in the core
// wforge library so both the tests and the editor use it.
//
// Memory-bounding contract: a snapshot only populates the data classes named
// in the action's ActionFlags (a texture paint stores alpha layers, never the
// 145 height floats), and the stack itself is a bounded deque that discards
// the oldest actions past a limit. Vanilla 1.12 ADTs carry no MCCV, so there
// is deliberately no vertex-color data class.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "math.hpp"

namespace wf {

// Data classes an action may modify. begin() declares the union of classes a
// tool can touch; touchChunk()/commit() snapshot ONLY those classes.
enum ActionFlags : uint32_t {
    AF_Heights        = 0x1,     // MCVT 145-float height grid
    AF_Alpha          = 0x2,     // decoded 64x64 alpha working buffers
    AF_Holes          = 0x4,     // MCNK low-res hole bitmap
    AF_AreaId         = 0x8,     // MCNK area id
    AF_ChunkFlags     = 0x10,    // MCNK header flags
    AF_Shadow         = 0x20,    // MCSH 64x64 shadow bitmap
    AF_Liquid         = 0x40,    // MCLQ blob (opaque to the stack)
    AF_ObjAdded       = 0x80,    // placements created
    AF_ObjRemoved     = 0x100,   // placements deleted
    AF_ObjTransformed = 0x200,   // placements moved/rotated/scaled
};

// Identifies one MCNK chunk on the continent: WDT tile indices + the chunk's
// 0..255 index within the tile. Ordered so it can key a std::map.
struct ChunkKey {
    int tileX      = 0;
    int tileY      = 0;
    int chunkIndex = 0;

    bool operator<(const ChunkKey& o) const {
        if (tileX != o.tileX) return tileX < o.tileX;
        if (tileY != o.tileY) return tileY < o.tileY;
        return chunkIndex < o.chunkIndex;
    }
    bool operator==(const ChunkKey& o) const {
        return tileX == o.tileX && tileY == o.tileY && chunkIndex == o.chunkIndex;
    }
    bool operator!=(const ChunkKey& o) const { return !(*this == o); }
};

// One chunk's saved state. Every member is optional and is engaged ONLY when
// the owning action's flags name that data class -- this is the memory-
// bounding contract, and the tests assert it (an AF_Alpha action must leave
// `heights` empty). Alpha layers are the decoded 64x64 working buffers, one
// vector per layer; `liquidBlob` is an opaque byte image of the liquid data.
struct ChunkSnapshot {
    std::optional<std::array<float, 145>>          heights;
    std::optional<std::vector<std::vector<uint8_t>>> alphaLayers;
    std::optional<uint16_t>                        holes;
    std::optional<uint32_t>                        areaId;
    std::optional<uint32_t>                        chunkFlags;
    std::optional<std::vector<uint8_t>>            shadow;
    std::optional<std::vector<uint8_t>>            liquidBlob;
};

// Full placement record for an added or removed object -- enough for the host
// to re-create (undo a delete) or delete (undo an add) the placement.
struct ObjRec {
    uint32_t    uniqueId = 0;
    bool        isWmo    = false;
    std::string model;               // archived path, .m2 or .wmo
    Vec3        pos;                 // world space
    Vec3        rotDeg;              // per-axis rotation, degrees
    float       scale    = 1.0f;     // uniform factor
};

// Before/after transform of an existing placement.
struct ObjTransformState {
    Vec3  pos;
    Vec3  rotDeg;
    float scale = 1.0f;
};
struct ObjTransformRec {
    uint32_t          uniqueId = 0;
    bool              isWmo    = false;
    ObjTransformState before;
    ObjTransformState after;
};

// How consecutive commits with the same non-empty mergeKey combine:
//   Disable -- never merge; every commit is its own action.
//   Ends    -- collapse into one action keeping the FIRST pre-snapshot and
//              the LAST post-snapshot (the slider-drag case: N tweaks of the
//              same field undo in one step, back to the original value).
//   All     -- same-key actions merge outright. With snapshot-based actions
//              this collapses to the same stored state as Ends (first pre,
//              last post); the distinct mode is kept so hosts can express
//              intent and future replay-based actions can diverge.
// Merging only ever happens with the action at the top of the stack: a
// different-key action in between (or an undo) breaks the chain.
enum class MergeMode { Disable, Ends, All };

// One committed action: flag-gated pre/post snapshots per touched chunk plus
// object add/remove/transform records.
struct EditAction {
    uint32_t    flags       = 0;
    std::string label;
    uint64_t    timestampMs = 0;   // wall clock, ms since epoch (history panel)
    // Per chunk: .first = PRE (state before the action), .second = POST.
    std::map<ChunkKey, std::pair<ChunkSnapshot, ChunkSnapshot>> chunks;
    std::vector<ObjRec>          added;
    std::vector<ObjRec>          removed;
    std::vector<ObjTransformRec> transformed;
    std::string mergeKey;
    MergeMode   merge = MergeMode::Disable;
};

// The last RELATIVE transform delta, for repeat-last-transform (TrenchBroom
// Ctrl+R): replay the same delta against a new selection. Deltas, never
// absolute end states -- so repeating a "+5 north" nudge moves the next
// object +5 north from wherever it is.
struct RepeatDelta {
    Vec3  translation;
    Vec3  axisAngleDeg;         // rotation axis * angle in degrees
    float scaleFactor = 1.0f;
    bool  valid       = false;
};

// Host-supplied capture callback. `capture` reads the live chunk into `out`,
// engaging ONLY the optionals whose data classes appear in `flags`.
struct CaptureFns {
    std::function<void(const ChunkKey&, ChunkSnapshot& out, uint32_t flags)> capture;
};

// Host-supplied apply callbacks, the inverse of capture. `applyChunk` writes
// every engaged optional of `snap` back into the live chunk. The object
// callbacks re-create / delete / re-transform placements; `toAfter` selects
// which side of an ObjTransformRec to apply (false on undo, true on redo).
struct ApplyFns {
    std::function<void(const ChunkKey&, const ChunkSnapshot& snap, uint32_t flags)> applyChunk;
    std::function<void(const ObjRec&)>                       addObject;
    std::function<void(const ObjRec&)>                       removeObject;
    std::function<void(const ObjTransformRec&, bool toAfter)> transformObject;
};

// Bounded undo/redo stack of EditActions. Usage per stroke:
//   stack.begin(AF_Heights, "Raise terrain");
//   stack.touchChunk(key, cap);   // once per chunk the brush enters (COW)
//   ...tool mutates the live data model...
//   stack.commit(cap);            // seals POST snapshots and pushes
// The cursor sits BETWEEN actions: cursor() == N means actions [0,N) are
// applied. undo() steps it down re-applying pre-snapshots, redo() steps it up
// re-applying post-snapshots. Committing while undone discards the redo tail.
class CommandStack {
public:
    // Open a new action. Any previously open (uncommitted) action is
    // discarded. `flags` gates what the snapshots store; `mergeKey` + `merge`
    // drive coalescing of consecutive commits (see MergeMode).
    void begin(uint32_t flags, std::string label,
               MergeMode merge = MergeMode::Disable, std::string mergeKey = {});
    bool isOpen() const { return open_; }

    // Copy-on-write capture: the FIRST touch of a chunk during the open
    // action stores its PRE snapshot (flagged classes only); later touches of
    // the same chunk are no-ops. No open action: no-op.
    void touchChunk(const ChunkKey& key, const CaptureFns& fns);

    // Record object edits into the open action. Added/removed take the full
    // placement record; transformed takes the before/after states.
    void recordObjAdded(const ObjRec& rec);
    void recordObjRemoved(const ObjRec& rec);
    void recordObjTransformed(const ObjTransformRec& rec);

    // Stash the relative delta of the open action's transform; latched into
    // lastRepeatDelta() when the action commits (dropped on cancel()).
    void recordRepeatDelta(const RepeatDelta& delta);

    // Seal the open action: capture POST snapshots for every touched chunk,
    // drop any redo tail, apply the merge rule, push. An action that touched
    // no chunks and recorded no objects pushes nothing.
    void commit(const CaptureFns& fns);

    // Discard the open action without pushing (Escape during a gizmo drag).
    // Does not touch the stack or the live data model.
    void cancel();

    // Undo/redo one action via the host's apply callbacks. Return false at
    // the ends of the stack (cursor 0 / top) or while an action is open.
    bool undo(const ApplyFns& fns);
    bool redo(const ApplyFns& fns);

    // Multi-step for a history-navigator panel: land on the exact state as of
    // action `index` having been applied (undoing or redoing as needed).
    // Out-of-range index or an open action: no-op.
    void jumpTo(size_t index, const ApplyFns& fns);

    // Cap the deque; the oldest APPLIED actions are discarded from the front
    // (their edits become permanent). Unapplied actions ahead of the cursor
    // are never front-trimmed -- redo would otherwise replay history with a
    // hole -- so any remaining overflow drops from the back of the redo tail.
    // Never invalidates the cursor.
    void setLimit(size_t n = 30);
    size_t limit() const { return limit_; }

    size_t size() const   { return actions_.size(); }
    size_t cursor() const { return cursor_; }
    const EditAction& at(size_t index) const { return actions_[index]; }

    const RepeatDelta& lastRepeatDelta() const { return lastDelta_; }

private:
    void applyAction(const EditAction& action, const ApplyFns& fns, bool forward) const;
    void mergeInto(EditAction& top, EditAction&& incoming) const;
    void trimToLimit();

    std::deque<EditAction> actions_;
    size_t      cursor_ = 0;        // actions [0, cursor_) are applied
    size_t      limit_  = 30;
    bool        open_   = false;
    EditAction  pending_;           // the open action being recorded
    RepeatDelta pendingDelta_;      // staged by recordRepeatDelta()
    RepeatDelta lastDelta_;         // latched on commit
};

} // namespace wf
