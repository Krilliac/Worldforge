#include "test.hpp"
#include "imgui.h"

#include "editor/TexturePaintPanel.hpp"
#include "command_stack.hpp"
#include "terrain.hpp"
#include "coords.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

using namespace wf;
using namespace wf::editor;

void test_texture_paint() {
    std::printf("[editor.texture_paint]\n");

    // One chunk at tile 32,32 Index(0,0): corner (NW) at world (0,0), so it spans
    // world X,Y in [-CHUNK, 0]; its centre is at (-CHUNK/2, -CHUNK/2).
    MapChunk mc; mc.indexX = 0; mc.indexY = 0; mc.position = {0, 0, 0};
    std::vector<MapChunk> chunks = { mc };

    // chunkAlphas[0] carries one blend map (layer slot 1), starting fully clear.
    std::vector<std::vector<AlphaMap>> alphas(1);
    alphas[0].push_back(AlphaMap{});           // all-zero coverage

    TexturePaintPanel tp;
    tp.layer_    = 1;
    tp.radius_   = static_cast<float>(CHUNK_SIZE) * 0.25f;
    tp.strength_ = 1.0f;
    tp.falloff_  = 0;                           // Flat: full strength inside radius
    tp.erase_    = false;

    const Vec3 centre = { -static_cast<float>(CHUNK_SIZE) * 0.5f,
                          -static_cast<float>(CHUNK_SIZE) * 0.5f, 0.0f };

    int painted = tp.paint(alphas, chunks, 32, 32, centre);
    CHECK(painted > 0);
    CHECK(alphas[0][0].at(32, 32) == 255);     // chunk centre fully painted
    CHECK(alphas[0][0].at(0, 0)   == 0);       // a corner is outside the radius

    // Erase pulls the painted centre back toward 0.
    tp.erase_ = true;
    tp.paint(alphas, chunks, 32, 32, centre);
    CHECK(alphas[0][0].at(32, 32) == 0);

    // A slot the chunk doesn't have is a no-op (no crash, no hits).
    tp.erase_ = false;
    tp.layer_ = 3;                             // chunkAlphas[0] has only slot 1
    CHECK(tp.paint(alphas, chunks, 32, 32, centre) == 0);

    // Seamless across a shared border: two side-by-side chunks, brush centred on
    // the edge between them paints matching texels on both sides.
    {
        std::vector<MapChunk> two = {
            [] { MapChunk m; m.indexX = 0; m.indexY = 0; return m; }(),
            [] { MapChunk m; m.indexX = 1; m.indexY = 0; return m; }(),
        };
        std::vector<std::vector<AlphaMap>> ta(2);
        ta[0].push_back(AlphaMap{});
        ta[1].push_back(AlphaMap{});
        TexturePaintPanel ep;
        ep.layer_ = 1; ep.radius_ = static_cast<float>(CHUNK_SIZE) * 0.3f;
        ep.strength_ = 1.0f; ep.falloff_ = 0;
        // Shared edge between chunk0 (col0) and chunk1 (col1) is world Y = -CHUNK,
        // mid-height world X = -CHUNK/2.
        const Vec3 edge = { -static_cast<float>(CHUNK_SIZE) * 0.5f,
                            -static_cast<float>(CHUNK_SIZE), 0.0f };
        int n = ep.paint(ta, two, 32, 32, edge);
        CHECK(n > 0);
        // chunk0's east column (col 63) and chunk1's west column (col 0) are the
        // adjacent texels at the seam -- both should have been painted.
        CHECK(ta[0][0].at(32, 63) > 0);
        CHECK(ta[1][0].at(32, 0)  > 0);
    }

    // --- undo wiring: a paint stroke is ONE AF_Alpha action ------------------
    {
        MapChunk mc; mc.indexX = 0; mc.indexY = 0;
        std::vector<MapChunk> pchunks = { mc };
        std::vector<std::vector<AlphaMap>> alphas(1);
        alphas[0].push_back(AlphaMap{});
        const std::array<uint8_t, 64 * 64> original = alphas[0][0].texels;

        CommandStack stack;
        ApplyFns app;
        app.applyChunk = [&alphas](const ChunkKey& key, const ChunkSnapshot& snap,
                                   uint32_t) {
            if (!snap.alphaLayers) return;
            const size_t i = static_cast<size_t>(key.chunkIndex);
            if (i >= alphas.size()) return;
            const auto& layers = *snap.alphaLayers;
            for (size_t s = 0; s < layers.size() && s < alphas[i].size(); ++s)
                std::copy(layers[s].begin(), layers[s].end(),
                          alphas[i][s].texels.begin());
        };

        TexturePaintPanel pp;
        pp.setCommandStack(&stack);
        pp.layer_ = 1;
        pp.radius_ = static_cast<float>(CHUNK_SIZE) * 0.25f;
        pp.falloff_ = 0;

        pp.paint(alphas, pchunks, 32, 32, centre);
        CHECK(pp.strokeOpen());
        CHECK(stack.isOpen());
        pp.paint(alphas, pchunks, 32, 32, centre);       // second stroke step
        pp.endStroke();
        CHECK(!pp.strokeOpen());
        CHECK(stack.size() == 1);                        // whole drag = ONE action
        CHECK(alphas[0][0].at(32, 32) == 255);

        // Wave-1 memory contract: an AF_Alpha action stores NO height snapshot.
        const auto& snaps = stack.at(0).chunks.begin()->second;
        CHECK(!snaps.first.heights.has_value());
        CHECK(!snaps.second.heights.has_value());
        CHECK(snaps.first.alphaLayers.has_value());
        CHECK(snaps.second.alphaLayers.has_value());

        CHECK(stack.undo(app));                          // texels restored exactly
        CHECK(alphas[0][0].texels == original);
        CHECK(stack.redo(app));
        CHECK(alphas[0][0].at(32, 32) == 255);

        // A stroke whose brush reaches no chunk commits nothing.
        pp.paint(alphas, pchunks, 32, 32, Vec3{9000.0f, 9000.0f, 0});
        pp.endStroke();
        CHECK(stack.size() == 1);
    }

    // --- opacity hotkeys (Alt+1..5) are pure state ---------------------------
    {
        TexturePaintPanel hp;
        hp.setOpacityHotkey(1); CHECK(hp.targetAlpha_ == 255);
        hp.setOpacityHotkey(2); CHECK(hp.targetAlpha_ == 191);
        hp.setOpacityHotkey(3); CHECK(hp.targetAlpha_ == 127);
        hp.setOpacityHotkey(4); CHECK(hp.targetAlpha_ == 63);
        hp.setOpacityHotkey(5); CHECK(hp.targetAlpha_ == 0);
        hp.setOpacityHotkey(6); CHECK(hp.targetAlpha_ == 0);   // out of range: kept
        hp.setOpacityHotkey(0); CHECK(hp.targetAlpha_ == 0);
    }

    // --- target alpha + pressure converge on the target ----------------------
    {
        MapChunk mc; mc.indexX = 0; mc.indexY = 0;
        std::vector<MapChunk> pchunks = { mc };
        std::vector<std::vector<AlphaMap>> alphas(1);
        alphas[0].push_back(AlphaMap{});

        TexturePaintPanel pp;
        pp.layer_ = 1;
        pp.radius_ = static_cast<float>(CHUNK_SIZE) * 0.25f;
        pp.falloff_ = 0;
        pp.targetAlpha_ = 100;
        pp.pressure_ = 0.5f;
        pp.paint(alphas, pchunks, 32, 32, centre);
        CHECK(alphas[0][0].at(32, 32) == 50);            // half-way per step
        pp.pressure_ = 1.0f;
        pp.paint(alphas, pchunks, 32, 32, centre);
        CHECK(alphas[0][0].at(32, 32) == 100);           // lands on the target
        pp.paint(alphas, pchunks, 32, 32, centre);
        CHECK(alphas[0][0].at(32, 32) == 100);           // never overshoots

        // erase_ stays 'toward 0' regardless of the target level.
        pp.erase_ = true;
        pp.paint(alphas, pchunks, 32, 32, centre);
        CHECK(alphas[0][0].at(32, 32) == 0);
    }

    // --- spray scatter: deterministic per seed, session RNG advances ---------
    {
        auto sprayOnce = [&](uint32_t seed, AlphaMap& out) {
            MapChunk mc; mc.indexX = 0; mc.indexY = 0;
            std::vector<MapChunk> pchunks = { mc };
            std::vector<std::vector<AlphaMap>> alphas(1);
            alphas[0].push_back(AlphaMap{});
            TexturePaintPanel pp;
            pp.layer_ = 1; pp.falloff_ = 0;
            pp.spray_ = true;
            pp.sprayOuterRadius_ = static_cast<float>(CHUNK_SIZE) * 0.2f;
            pp.sprayDabRadius_   = static_cast<float>(CHUNK_SIZE) * 0.05f;
            pp.sprayDabsPerStep_ = 8;
            pp.sprayRng_ = seed;
            const int hits = pp.paint(alphas, pchunks, 32, 32, centre);
            CHECK(hits > 0);
            CHECK(pp.sprayRng_ != seed);                 // the session RNG advanced
            out = alphas[0][0];
        };
        AlphaMap a, b;
        sprayOnce(1234u, a);
        sprayOnce(1234u, b);
        CHECK(a.texels == b.texels);                     // same seed, same scatter
    }

    // --- draw() smoke test (headless ImGui) ---------------------------------
    ImGui::NewFrame();
    tp.draw();
    ImGui::Render();
}
