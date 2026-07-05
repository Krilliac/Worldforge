#include "test.hpp"
#include "imgui.h"

#include "editor/TerrainToolPanel.hpp"
#include "command_stack.hpp"
#include "terrain.hpp"
#include "coords.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace wf;
using namespace wf::editor;

// Build an NxN flat grid of vertices at z=0, spread over [-half, +half] in X/Y,
// centred on the origin. Spacing is `step` yards between adjacent vertices.
static Mesh makeFlatGrid(int n, float step) {
    Mesh m;
    const float half = (n - 1) * step * 0.5f;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            Vertex v;
            v.position = { i * step - half, j * step - half, 0.0f };
            v.normal   = { 0, 0, 1 };
            m.vertices.push_back(v);
        }
    return m;
}

void test_terrain_tool() {
    std::printf("[editor.terrain_tool]\n");

    // 9x9 grid, 10 yards apart -> spans [-40, +40] in X and Y.
    const int N = 9;
    const float STEP = 10.0f;
    Mesh mesh = makeFlatGrid(N, STEP);

    // A vertex far from the centre (a corner) and the exact centre vertex.
    const int centreIdx = (N / 2) * N + (N / 2);   // (4,4) -> origin
    const int cornerIdx = 0;                        // (-40,-40)
    CHECK_APPROX(mesh.vertices[centreIdx].position.x, 0.0f);
    CHECK_APPROX(mesh.vertices[centreIdx].position.y, 0.0f);

    // --- Raise: radius covers a few central rings but not the corners --------
    TerrainToolPanel panel;
    panel.mode_     = static_cast<int>(TerrainToolPanel::Mode::Raise);
    panel.radius_   = 25.0f;     // reaches the 2nd ring (20 yd) but not 30+ yd
    panel.strength_ = 2.0f;
    panel.falloff_  = 0;         // Flat: full strength everywhere inside radius

    const int raised = panel.apply(mesh, Vec3{0, 0, 0});
    CHECK(raised > 0);

    // The centre vertex rose; a far corner is untouched.
    CHECK(mesh.vertices[centreIdx].position.z > 0.0f);
    CHECK_APPROX(mesh.vertices[cornerIdx].position.z, 0.0f);

    // Flat falloff + strength 2 -> centre raised by exactly 2 per stroke.
    CHECK_APPROX(mesh.vertices[centreIdx].position.z, 2.0f);

    // --- Lower: a subsequent apply pulls the raised heights back down --------
    const float beforeLower = mesh.vertices[centreIdx].position.z;
    panel.mode_ = static_cast<int>(TerrainToolPanel::Mode::Lower);
    const int lowered = panel.apply(mesh, Vec3{0, 0, 0});
    CHECK(lowered > 0);
    CHECK(mesh.vertices[centreIdx].position.z < beforeLower);
    // Raise(+2) then Lower(-2) with Flat falloff returns the centre to 0.
    CHECK_APPROX(mesh.vertices[centreIdx].position.z, 0.0f);
    // The corner outside the brush is still untouched.
    CHECK_APPROX(mesh.vertices[cornerIdx].position.z, 0.0f);

    // --- applyChunks: sculpt a real tile's source MCNK height grids ----------
    // One flat chunk at tile 32,32 Index(0,0): its NW corner sample sits at world
    // (0,0), so a small brush there raises MCVT[0] in place (the export-ready path).
    {
        MapChunk mc;
        mc.indexX = 0; mc.indexY = 0; mc.position = {0, 0, 0}; mc.heights.fill(0.0f);
        std::vector<MapChunk> chunks = { mc };

        TerrainToolPanel tp;
        tp.mode_ = static_cast<int>(TerrainToolPanel::Mode::Raise);
        tp.radius_ = 2.0f; tp.strength_ = 4.0f; tp.falloff_ = 0;  // Flat
        const int hit = tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0});
        CHECK(hit == 1);                                   // only the corner sample
        CHECK_APPROX(chunks[0].heights[0], 4.0f);          // MCVT[0] raised in place

        // Flatten toward the picked Z (here 0) pulls the spike back down.
        tp.mode_ = static_cast<int>(TerrainToolPanel::Mode::Flatten);
        tp.strength_ = 1.0f;
        tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0});
        CHECK_APPROX(chunks[0].heights[0], 0.0f);
    }

    // --- undo wiring: a 3-step stroke is exactly ONE action ------------------
    // Scripted stroke (3x applyChunks + endStroke) against a command stack:
    // one action on commit, undo restores every MCVT float byte-exactly, redo
    // re-applies the edit.
    {
        MapChunk mc;
        mc.indexX = 0; mc.indexY = 0; mc.position = {0, 0, 0};
        for (size_t i = 0; i < mc.heights.size(); ++i)
            mc.heights[i] = 0.25f * static_cast<float>(i);
        std::vector<MapChunk> chunks = { mc };
        const std::array<float, 145> original = chunks[0].heights;

        CommandStack stack;
        ApplyFns app;
        app.applyChunk = [&chunks](const ChunkKey& key, const ChunkSnapshot& snap,
                                   uint32_t) {
            if (snap.heights && key.chunkIndex >= 0 &&
                static_cast<size_t>(key.chunkIndex) < chunks.size())
                chunks[key.chunkIndex].heights = *snap.heights;
        };

        TerrainToolPanel tp;
        tp.setCommandStack(&stack);
        tp.mode_ = static_cast<int>(TerrainToolPanel::Mode::Raise);
        tp.radius_ = 6.0f; tp.strength_ = 1.5f; tp.falloff_ = 0;

        CHECK(tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0}) > 0);
        CHECK(tp.strokeOpen());
        CHECK(stack.isOpen());
        // Undo mid-stroke is blocked: cursor unchanged while the action is open.
        CHECK(!stack.undo(app));
        CHECK(stack.cursor() == 0);

        tp.applyChunks(chunks, 32, 32, Vec3{-2, 0, 0});
        tp.applyChunks(chunks, 32, 32, Vec3{-4, 0, 0});
        tp.endStroke();
        CHECK(!tp.strokeOpen());
        CHECK(stack.size() == 1);                        // whole drag = ONE action
        CHECK(stack.cursor() == 1);

        const std::array<float, 145> edited = chunks[0].heights;
        CHECK(std::memcmp(edited.data(), original.data(), sizeof original) != 0);

        CHECK(stack.undo(app));                          // all 145 floats restored
        CHECK(std::memcmp(chunks[0].heights.data(), original.data(),
                          sizeof original) == 0);
        CHECK(stack.redo(app));                          // redo re-applies
        CHECK(std::memcmp(chunks[0].heights.data(), edited.data(),
                          sizeof edited) == 0);

        // A stroke whose brush reaches no chunk commits nothing.
        tp.applyChunks(chunks, 32, 32, Vec3{5000.0f, 5000.0f, 0});
        tp.endStroke();
        CHECK(stack.size() == 1);
        CHECK(stack.cursor() == 1);
    }

    // --- Smooth mode (Ctrl-variant of Flatten) relaxes a spike ---------------
    {
        MapChunk mc;
        mc.indexX = 0; mc.indexY = 0; mc.position = {0, 0, 0}; mc.heights.fill(0.0f);
        mc.heights[0] = 8.0f;                            // spike at the NW corner
        std::vector<MapChunk> chunks = { mc };

        TerrainToolPanel tp;
        tp.mode_ = static_cast<int>(TerrainToolPanel::Mode::Smooth);
        tp.radius_ = 6.0f; tp.strength_ = 1.0f; tp.falloff_ = 0;
        CHECK(tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0}) > 0);
        CHECK(chunks[0].heights[0] < 8.0f);              // pulled toward neighbours
    }

    // --- FlattenMode gates movement; lock point + angle tilt the target ------
    {
        MapChunk mc;
        mc.indexX = 0; mc.indexY = 0; mc.position = {0, 0, 0}; mc.heights.fill(0.0f);
        std::vector<MapChunk> chunks = { mc };

        TerrainToolPanel tp;
        tp.mode_ = static_cast<int>(TerrainToolPanel::Mode::Flatten);
        tp.radius_ = 2.0f; tp.strength_ = 1.0f; tp.falloff_ = 0;   // corner sample only
        tp.useLock_ = true;
        tp.lockPoint_ = {0, 0, 5.0f};

        // RaiseOnly with the plane above lifts the corner sample to the target.
        tp.flattenMode_ = static_cast<int>(FlattenMode::RaiseOnly);
        CHECK(tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0}) == 1);
        CHECK_APPROX(chunks[0].heights[0], 5.0f);

        // RaiseOnly with the plane below may not move anything down: untouched.
        tp.lockPoint_.z = 1.0f;
        CHECK(tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0}) == 0);
        CHECK_APPROX(chunks[0].heights[0], 5.0f);

        // LowerOnly now brings it down onto the lower plane.
        tp.flattenMode_ = static_cast<int>(FlattenMode::LowerOnly);
        tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0});
        CHECK_APPROX(chunks[0].heights[0], 1.0f);

        // A 45-degree plane anchored 10 yds up-slope of the corner targets +10:
        //   lock.z + tan(45) * dot(p - lock, dir) = 0 + 1 * 10.
        tp.flattenMode_ = static_cast<int>(FlattenMode::Both);
        tp.lockPoint_ = {-10.0f, 0.0f, 0.0f};
        tp.orientationDeg_ = 0.0f;                       // up-slope = +X (north)
        tp.angleDeg_ = 45.0f;
        tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0});
        CHECK_APPROX(chunks[0].heights[0], 10.0f);
    }

    // --- wave-1 falloff profiles are selectable end-to-end -------------------
    // At the brush centre every profile weights 1.0, so a Raise lands the full
    // per-step strength regardless of the curve.
    {
        for (int fo = 4; fo <= 6; ++fo) {                // Polynomial/Trig/Quadratic
            MapChunk mc;
            mc.indexX = 0; mc.indexY = 0; mc.position = {0, 0, 0}; mc.heights.fill(0.0f);
            std::vector<MapChunk> chunks = { mc };
            TerrainToolPanel tp;
            tp.mode_ = static_cast<int>(TerrainToolPanel::Mode::Raise);
            tp.radius_ = 2.0f; tp.strength_ = 3.0f; tp.falloff_ = fo;
            CHECK(tp.applyChunks(chunks, 32, 32, Vec3{0, 0, 0}) == 1);
            CHECK_APPROX(chunks[0].heights[0], 3.0f);
        }
    }

    // --- draw() smoke test (headless ImGui) ---------------------------------
    ImGui::NewFrame();
    panel.draw();
    ImGui::Render();
}
