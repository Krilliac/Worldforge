#include "command_stack.hpp"

#include <chrono>

namespace wf {

namespace {

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

bool sameObject(const ObjTransformRec& a, const ObjTransformRec& b) {
    return a.uniqueId == b.uniqueId && a.isWmo == b.isWmo;
}

} // namespace

void CommandStack::begin(uint32_t flags, std::string label,
                         MergeMode merge, std::string mergeKey) {
    // A still-open action is a tool bug; drop it rather than corrupt state.
    pending_ = EditAction{};
    pending_.flags       = flags;
    pending_.label       = std::move(label);
    pending_.merge       = merge;
    pending_.mergeKey    = std::move(mergeKey);
    pending_.timestampMs = nowMs();
    pendingDelta_        = RepeatDelta{};
    open_                = true;
}

void CommandStack::touchChunk(const ChunkKey& key, const CaptureFns& fns) {
    if (!open_) return;
    if (pending_.chunks.count(key)) return;       // COW: first touch only
    ChunkSnapshot pre;
    if (fns.capture) fns.capture(key, pre, pending_.flags);
    pending_.chunks.emplace(key, std::make_pair(std::move(pre), ChunkSnapshot{}));
}

void CommandStack::recordObjAdded(const ObjRec& rec) {
    if (open_) pending_.added.push_back(rec);
}

void CommandStack::recordObjRemoved(const ObjRec& rec) {
    if (open_) pending_.removed.push_back(rec);
}

void CommandStack::recordObjTransformed(const ObjTransformRec& rec) {
    if (open_) pending_.transformed.push_back(rec);
}

void CommandStack::recordRepeatDelta(const RepeatDelta& delta) {
    if (open_) pendingDelta_ = delta;
}

void CommandStack::commit(const CaptureFns& fns) {
    if (!open_) return;
    open_ = false;
    EditAction action = std::move(pending_);
    pending_ = EditAction{};

    // An action that changed nothing leaves no trace on the stack.
    if (action.chunks.empty() && action.added.empty() &&
        action.removed.empty() && action.transformed.empty()) {
        pendingDelta_ = RepeatDelta{};
        return;
    }

    // Seal every touched chunk with its POST snapshot.
    for (auto& entry : action.chunks)
        if (fns.capture) fns.capture(entry.first, entry.second.second, action.flags);

    // Committing while undone discards the redo tail.
    while (actions_.size() > cursor_) actions_.pop_back();

    // Merge rule: only ever with the action now at the top of the stack --
    // a different-key action in between (or an undo) breaks the chain, so a
    // merge across a non-adjacent action never happens.
    bool merged = false;
    if (action.merge != MergeMode::Disable && !action.mergeKey.empty() &&
        !actions_.empty()) {
        EditAction& top = actions_.back();
        if (top.merge != MergeMode::Disable && top.mergeKey == action.mergeKey) {
            mergeInto(top, std::move(action));
            merged = true;
        }
    }
    if (!merged) {
        actions_.push_back(std::move(action));
        ++cursor_;
        trimToLimit();
    }

    if (pendingDelta_.valid) lastDelta_ = pendingDelta_;
    pendingDelta_ = RepeatDelta{};
}

void CommandStack::cancel() {
    open_         = false;
    pending_      = EditAction{};
    pendingDelta_ = RepeatDelta{};
}

bool CommandStack::undo(const ApplyFns& fns) {
    if (open_ || cursor_ == 0) return false;
    applyAction(actions_[cursor_ - 1], fns, /*forward=*/false);
    --cursor_;
    return true;
}

bool CommandStack::redo(const ApplyFns& fns) {
    if (open_ || cursor_ >= actions_.size()) return false;
    applyAction(actions_[cursor_], fns, /*forward=*/true);
    ++cursor_;
    return true;
}

void CommandStack::jumpTo(size_t index, const ApplyFns& fns) {
    if (open_ || index >= actions_.size()) return;
    const size_t target = index + 1;    // state as of action `index` applied
    while (cursor_ > target) {
        applyAction(actions_[cursor_ - 1], fns, /*forward=*/false);
        --cursor_;
    }
    while (cursor_ < target) {
        applyAction(actions_[cursor_], fns, /*forward=*/true);
        ++cursor_;
    }
}

void CommandStack::setLimit(size_t n) {
    limit_ = n > 0 ? n : 1;
    trimToLimit();
}

void CommandStack::applyAction(const EditAction& action, const ApplyFns& fns,
                               bool forward) const {
    if (fns.applyChunk)
        for (const auto& entry : action.chunks)
            fns.applyChunk(entry.first,
                           forward ? entry.second.second : entry.second.first,
                           action.flags);
    if (forward) {
        // Redo: replay the action -- re-add its adds, re-delete its deletes.
        if (fns.addObject)
            for (const ObjRec& rec : action.added) fns.addObject(rec);
        if (fns.removeObject)
            for (const ObjRec& rec : action.removed) fns.removeObject(rec);
        if (fns.transformObject)
            for (const ObjTransformRec& rec : action.transformed)
                fns.transformObject(rec, /*toAfter=*/true);
    } else {
        // Undo: invert -- delete its adds, resurrect its deletes.
        if (fns.removeObject)
            for (const ObjRec& rec : action.added) fns.removeObject(rec);
        if (fns.addObject)
            for (const ObjRec& rec : action.removed) fns.addObject(rec);
        if (fns.transformObject)
            for (const ObjTransformRec& rec : action.transformed)
                fns.transformObject(rec, /*toAfter=*/false);
    }
}

// Fold `incoming` into `top` keeping the FIRST pre-snapshot and the LAST
// post-snapshot per chunk (Ends semantics; with snapshot-based actions All
// stores the same state -- see MergeMode).
void CommandStack::mergeInto(EditAction& top, EditAction&& incoming) const {
    top.flags       |= incoming.flags;
    top.timestampMs  = incoming.timestampMs;
    for (auto& entry : incoming.chunks) {
        auto it = top.chunks.find(entry.first);
        if (it != top.chunks.end())
            it->second.second = std::move(entry.second.second);   // last post
        else
            top.chunks.emplace(entry.first, std::move(entry.second));
    }
    for (ObjRec& rec : incoming.added)   top.added.push_back(std::move(rec));
    for (ObjRec& rec : incoming.removed) top.removed.push_back(std::move(rec));
    for (ObjTransformRec& rec : incoming.transformed) {
        bool folded = false;
        for (ObjTransformRec& existing : top.transformed) {
            if (sameObject(existing, rec)) {
                existing.after = rec.after;      // keep first before, last after
                folded = true;
                break;
            }
        }
        if (!folded) top.transformed.push_back(std::move(rec));
    }
}

void CommandStack::trimToLimit() {
    while (actions_.size() > limit_) {
        actions_.pop_front();
        if (cursor_ > 0) --cursor_;    // the dropped action is now permanent
    }
}

} // namespace wf
