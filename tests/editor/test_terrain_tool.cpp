#include "test.hpp"
#include "imgui.h"

#include "editor/TerrainToolPanel.hpp"
#include "terrain.hpp"

#include <cstdio>

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

    // --- draw() smoke test (headless ImGui) ---------------------------------
    ImGui::NewFrame();
    panel.draw();
    ImGui::Render();
}
