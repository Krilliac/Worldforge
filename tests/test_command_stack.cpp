#include "test.hpp"
#include "command_stack.hpp"

#include <map>
#include <vector>

using namespace wf;

namespace {

// In-memory stand-in for the live terrain/scene data model. The stack never
// sees this type -- it only calls the capture/apply lambdas below, which is
// exactly the decoupling the real editor host relies on.
struct FakeChunk {
    std::array<float, 145>            heights{};
    std::vector<std::vector<uint8_t>> alphaLayers;
    uint16_t                          holes      = 0;
    uint32_t                          areaId     = 0;
    uint32_t                          chunkFlags = 0;
    std::vector<uint8_t>              shadow;
    std::vector<uint8_t>              liquidBlob;
};

struct FakeWorld {
    std::map<ChunkKey, FakeChunk> chunks;
    std::map<uint32_t, ObjRec>    objects;
    int captureCalls = 0;

    CaptureFns cap() {
        CaptureFns fns;
        fns.capture = [this](const ChunkKey& key, ChunkSnapshot& out, uint32_t flags) {
            ++captureCalls;
            const FakeChunk& c = chunks[key];
            if (flags & AF_Heights)    out.heights     = c.heights;
            if (flags & AF_Alpha)      out.alphaLayers = c.alphaLayers;
            if (flags & AF_Holes)      out.holes       = c.holes;
            if (flags & AF_AreaId)     out.areaId      = c.areaId;
            if (flags & AF_ChunkFlags) out.chunkFlags  = c.chunkFlags;
            if (flags & AF_Shadow)     out.shadow      = c.shadow;
            if (flags & AF_Liquid)     out.liquidBlob  = c.liquidBlob;
        };
        return fns;
    }

    ApplyFns app() {
        ApplyFns fns;
        fns.applyChunk = [this](const ChunkKey& key, const ChunkSnapshot& snap, uint32_t) {
            FakeChunk& c = chunks[key];
            if (snap.heights)     c.heights     = *snap.heights;
            if (snap.alphaLayers) c.alphaLayers = *snap.alphaLayers;
            if (snap.holes)       c.holes       = *snap.holes;
            if (snap.areaId)      c.areaId      = *snap.areaId;
            if (snap.chunkFlags)  c.chunkFlags  = *snap.chunkFlags;
            if (snap.shadow)      c.shadow      = *snap.shadow;
            if (snap.liquidBlob)  c.liquidBlob  = *snap.liquidBlob;
        };
        fns.addObject    = [this](const ObjRec& rec) { objects[rec.uniqueId] = rec; };
        fns.removeObject = [this](const ObjRec& rec) { objects.erase(rec.uniqueId); };
        fns.transformObject = [this](const ObjTransformRec& rec, bool toAfter) {
            auto it = objects.find(rec.uniqueId);
            if (it == objects.end()) return;
            const ObjTransformState& s = toAfter ? rec.after : rec.before;
            it->second.pos    = s.pos;
            it->second.rotDeg = s.rotDeg;
            it->second.scale  = s.scale;
        };
        return fns;
    }
};

} // namespace

void test_command_stack() {
    std::printf("[command_stack]\n");

    const ChunkKey key{3, 7, 42};

    // --- stroke -> undo restores byte-identical state; redo re-applies ------
    {
        FakeWorld world;
        FakeChunk& live = world.chunks[key];
        for (size_t i = 0; i < live.heights.size(); ++i)
            live.heights[i] = 0.5f * static_cast<float>(i);
        live.alphaLayers = { std::vector<uint8_t>(64, 7), std::vector<uint8_t>(64, 9) };
        live.holes = 0xBEEF;
        const FakeChunk original = live;

        CommandStack stack;
        stack.begin(AF_Heights | AF_Alpha | AF_Holes, "Sculpt");
        stack.touchChunk(key, world.cap());
        for (float& h : live.heights) h += 4.25f;      // the "stroke"
        live.alphaLayers[0].assign(64, 200);
        live.holes = 0;
        stack.commit(world.cap());
        const FakeChunk edited = live;

        CHECK(stack.size() == 1);
        CHECK(stack.cursor() == 1);
        CHECK(stack.undo(world.app()));
        CHECK(world.chunks[key].heights == original.heights);
        CHECK(world.chunks[key].alphaLayers == original.alphaLayers);
        CHECK(world.chunks[key].holes == original.holes);
        CHECK(stack.cursor() == 0);
        CHECK(!stack.undo(world.app()));               // cursor 0 -> false

        CHECK(stack.redo(world.app()));
        CHECK(world.chunks[key].heights == edited.heights);
        CHECK(world.chunks[key].alphaLayers == edited.alphaLayers);
        CHECK(world.chunks[key].holes == edited.holes);
        CHECK(!stack.redo(world.app()));               // at top -> false
    }

    // --- memory contract: AF_Alpha action stores NO height snapshot ---------
    {
        FakeWorld world;
        world.chunks[key].alphaLayers = { std::vector<uint8_t>(64, 1) };

        CommandStack stack;
        stack.begin(AF_Alpha, "Paint");
        stack.touchChunk(key, world.cap());
        world.chunks[key].alphaLayers[0].assign(64, 99);
        stack.commit(world.cap());

        const auto& snaps = stack.at(0).chunks.at(key);
        CHECK(!snaps.first.heights.has_value());       // pre: no heights
        CHECK(!snaps.second.heights.has_value());      // post: no heights
        CHECK(!snaps.first.shadow.has_value());
        CHECK(!snaps.first.liquidBlob.has_value());
        CHECK(snaps.first.alphaLayers.has_value());    // the flagged class IS there
        CHECK(snaps.second.alphaLayers.has_value());
    }

    // --- COW: touching the same chunk 100x captures exactly one pre ---------
    {
        FakeWorld world;
        CommandStack stack;
        stack.begin(AF_Heights, "Sculpt");
        for (int i = 0; i < 100; ++i) stack.touchChunk(key, world.cap());
        CHECK(world.captureCalls == 1);                // one PRE, not 100
        world.chunks[key].heights[0] = 1.0f;
        stack.commit(world.cap());
        CHECK(world.captureCalls == 2);                // + one POST
    }

    // --- commit with nothing recorded pushes nothing; cancel too ------------
    {
        FakeWorld world;
        CommandStack stack;
        stack.begin(AF_Heights, "No-op");
        stack.commit(world.cap());
        CHECK(stack.size() == 0);
        CHECK(stack.cursor() == 0);

        stack.begin(AF_Heights, "Escaped drag");
        stack.touchChunk(key, world.cap());
        stack.cancel();                                // Escape during a gizmo drag
        CHECK(!stack.isOpen());
        CHECK(stack.size() == 0);
        CHECK(stack.cursor() == 0);
    }

    // --- bounded deque: never exceeds the limit, oldest drops, cursor OK ----
    {
        FakeWorld world;
        CommandStack stack;
        stack.setLimit(3);
        for (uint32_t i = 0; i < 5; ++i) {
            stack.begin(AF_AreaId, "a" + std::to_string(i));
            stack.touchChunk(key, world.cap());
            world.chunks[key].areaId = i + 1;
            stack.commit(world.cap());
            CHECK(stack.size() <= 3);
        }
        CHECK(stack.size() == 3);
        CHECK(stack.cursor() == 3);                    // still valid after trims
        CHECK(stack.at(0).label == "a2");              // a0, a1 discarded
        CHECK(stack.at(2).label == "a4");

        CHECK(stack.undo(world.app()));
        CHECK(stack.undo(world.app()));
        CHECK(stack.undo(world.app()));
        CHECK(!stack.undo(world.app()));               // dropped actions are permanent
        CHECK(world.chunks[key].areaId == 2u);         // state after (discarded) a1
    }

    // --- MergeMode::Ends: 5 same-key commits -> 1 action, undo -> original --
    {
        FakeWorld world;
        world.chunks[key].areaId = 100;

        CommandStack stack;
        for (uint32_t i = 1; i <= 5; ++i) {
            stack.begin(AF_AreaId, "Set area", MergeMode::Ends, "areaSlider");
            stack.touchChunk(key, world.cap());
            world.chunks[key].areaId = 10 * i;
            stack.commit(world.cap());
        }
        CHECK(stack.size() == 1);
        CHECK(stack.cursor() == 1);
        CHECK(stack.undo(world.app()));
        CHECK(world.chunks[key].areaId == 100u);       // FIRST pre wins
        CHECK(stack.redo(world.app()));
        CHECK(world.chunks[key].areaId == 50u);        // LAST post wins

        // A different-key action in between breaks the chain: no merge across
        // a non-adjacent action.
        stack.begin(AF_ChunkFlags, "Flags", MergeMode::Ends, "flagToggle");
        stack.touchChunk(key, world.cap());
        world.chunks[key].chunkFlags = 0x2;
        stack.commit(world.cap());
        stack.begin(AF_AreaId, "Set area", MergeMode::Ends, "areaSlider");
        stack.touchChunk(key, world.cap());
        world.chunks[key].areaId = 60;
        stack.commit(world.cap());
        CHECK(stack.size() == 3);                      // slider action NOT merged into #0
    }

    // --- MergeMode::All merges outright; Disable never does -----------------
    {
        FakeWorld world;
        CommandStack stack;
        for (int i = 0; i < 3; ++i) {
            stack.begin(AF_Holes, "Holes", MergeMode::All, "holePunch");
            stack.touchChunk(key, world.cap());
            world.chunks[key].holes = static_cast<uint16_t>(i + 1);
            stack.commit(world.cap());
        }
        CHECK(stack.size() == 1);
        for (int i = 0; i < 3; ++i) {
            stack.begin(AF_Holes, "Holes", MergeMode::Disable, "holePunch");
            stack.touchChunk(key, world.cap());
            world.chunks[key].holes = static_cast<uint16_t>(10 + i);
            stack.commit(world.cap());
        }
        CHECK(stack.size() == 4);                      // Disable: one action each
    }

    // --- jumpTo lands on the exact state of index i, both directions --------
    {
        FakeWorld world;
        CommandStack stack;
        for (uint32_t i = 1; i <= 4; ++i) {
            stack.begin(AF_AreaId, "Step");
            stack.touchChunk(key, world.cap());
            world.chunks[key].areaId = i;
            stack.commit(world.cap());
        }
        stack.jumpTo(0, world.app());                  // backward 3 steps
        CHECK(world.chunks[key].areaId == 1u);
        CHECK(stack.cursor() == 1);
        stack.jumpTo(3, world.app());                  // forward 3 steps
        CHECK(world.chunks[key].areaId == 4u);
        CHECK(stack.cursor() == 4);
        stack.jumpTo(1, world.app());                  // backward again
        CHECK(world.chunks[key].areaId == 2u);
        CHECK(stack.cursor() == 2);
        stack.jumpTo(99, world.app());                 // out of range: no-op
        CHECK(stack.cursor() == 2);
    }

    // --- object add/remove/transform records undo and redo ------------------
    {
        FakeWorld world;
        ObjRec tree;
        tree.uniqueId = 7;
        tree.model    = "World\\tree.m2";
        tree.pos      = {10, 20, 30};
        tree.scale    = 1.0f;

        CommandStack stack;
        stack.begin(AF_ObjAdded, "Place tree");
        world.objects[tree.uniqueId] = tree;           // host performs the add...
        stack.recordObjAdded(tree);                    // ...and records it
        stack.commit(world.cap());
        CHECK(stack.size() == 1);

        CHECK(stack.undo(world.app()));
        CHECK(world.objects.count(7) == 0);            // undo of add = remove
        CHECK(stack.redo(world.app()));
        CHECK(world.objects.count(7) == 1);            // redo re-adds

        ObjTransformRec move;
        move.uniqueId   = 7;
        move.before     = {tree.pos, tree.rotDeg, tree.scale};
        move.after      = {{15, 20, 30}, {0, 0, 90}, 2.0f};
        stack.begin(AF_ObjTransformed, "Move tree");
        world.objects[7].pos    = move.after.pos;      // host applies the drag
        world.objects[7].rotDeg = move.after.rotDeg;
        world.objects[7].scale  = move.after.scale;
        stack.recordObjTransformed(move);
        stack.commit(world.cap());

        CHECK(stack.undo(world.app()));
        CHECK_APPROX(world.objects[7].pos.x, 10.0f);   // back to before
        CHECK_APPROX(world.objects[7].scale, 1.0f);
        CHECK(stack.redo(world.app()));
        CHECK_APPROX(world.objects[7].pos.x, 15.0f);
        CHECK_APPROX(world.objects[7].rotDeg.z, 90.0f);
        CHECK_APPROX(world.objects[7].scale, 2.0f);
    }

    // --- repeat-last-transform slot: relative delta round-trips -------------
    {
        FakeWorld world;
        CommandStack stack;
        CHECK(!stack.lastRepeatDelta().valid);         // empty until a commit

        RepeatDelta delta;
        delta.translation  = {5, 0, 0};
        delta.axisAngleDeg = {0, 0, 45};
        delta.scaleFactor  = 1.5f;
        delta.valid        = true;

        ObjTransformRec move;
        move.uniqueId = 1;
        stack.begin(AF_ObjTransformed, "Nudge");
        stack.recordObjTransformed(move);
        stack.recordRepeatDelta(delta);
        stack.commit(world.cap());

        const RepeatDelta& last = stack.lastRepeatDelta();
        CHECK(last.valid);
        CHECK_APPROX(last.translation.x, 5.0f);
        CHECK_APPROX(last.axisAngleDeg.z, 45.0f);
        CHECK_APPROX(last.scaleFactor, 1.5f);

        // A cancelled action never publishes its staged delta.
        RepeatDelta other;
        other.translation = {-99, 0, 0};
        other.valid       = true;
        stack.begin(AF_ObjTransformed, "Escaped nudge");
        stack.recordRepeatDelta(other);
        stack.cancel();
        CHECK_APPROX(stack.lastRepeatDelta().translation.x, 5.0f);   // unchanged
    }

    // --- committing while undone discards the redo tail ---------------------
    {
        FakeWorld world;
        CommandStack stack;
        for (uint32_t i = 1; i <= 3; ++i) {
            stack.begin(AF_AreaId, "Step");
            stack.touchChunk(key, world.cap());
            world.chunks[key].areaId = i;
            stack.commit(world.cap());
        }
        stack.undo(world.app());
        stack.undo(world.app());
        CHECK(stack.cursor() == 1);
        stack.begin(AF_AreaId, "Branch");
        stack.touchChunk(key, world.cap());
        world.chunks[key].areaId = 77;
        stack.commit(world.cap());
        CHECK(stack.size() == 2);                      // steps 2 and 3 are gone
        CHECK(stack.cursor() == 2);
        CHECK(stack.at(1).label == "Branch");
        CHECK(!stack.redo(world.app()));
    }
}
