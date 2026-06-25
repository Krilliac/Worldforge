#include "test.hpp"
#include "imgui.h"

#include "editor/TexturePaintPanel.hpp"
#include "terrain.hpp"
#include "coords.hpp"

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

    // --- draw() smoke test (headless ImGui) ---------------------------------
    ImGui::NewFrame();
    tp.draw();
    ImGui::Render();
}
