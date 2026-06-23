#include "test.hpp"
#include "imgui.h"

#include "editor/Camera.hpp"
#include "editor/ViewportPanel.hpp"
#include "editor/SoftwareImGui.hpp"
#include "editor/GizmoController.hpp"
#include "world_view.hpp"
#include "picking.hpp"
#include "terrain.hpp"
#include "debugdraw.hpp"
#include "image.hpp"

#include <vector>

using namespace wf;
using namespace wf::editor;

void test_viewport() {
    std::printf("[editor.viewport]\n");

    // --- camera basis + movement -------------------------------------------
    Camera cam;
    cam.eye = {0, 0, 0}; cam.yaw = 0; cam.pitch = 0;
    Vec3 f = cam.forward();
    CHECK_APPROX(f.x, 1.0f); CHECK_APPROX(f.y, 0.0f); CHECK_APPROX(f.z, 0.0f);
    Vec3 r = cam.right();                       // cross(+X, +Z) = -Y
    CHECK_APPROX(r.y, -1.0f);
    cam.fly(10.0f, 0, 0);                        // forward 10 -> +X
    CHECK_APPROX(cam.eye.x, 10.0f);
    cam.look(0.5f, -3.0f);                       // pitch clamps near -vertical
    CHECK(cam.pitch >= -1.55f);
    CHECK_APPROX(cam.yaw, 0.5f);

    // --- viewport renders a non-empty scene image ---------------------------
    Mesh m;
    m.vertices = { {{-5,-5,0},{0,0,1}}, {{5,-5,0},{0,0,1}}, {{0,5,0},{0,0,1}} };
    m.indices = { 0, 1, 2 };
    DebugDraw dd;
    ViewportPanel vp(160, 120);
    vp.camera.eye = {0, 0, 30}; vp.camera.yaw = 0; vp.camera.pitch = -1.55f; // look down
    vp.render(m, dd);
    CHECK(vp.scene().width == 160 && vp.scene().height == 120);
    bool any = false;
    for (const Rgba& p : vp.scene().pixels)
        if (!(p.r == 18 && p.g == 20 && p.b == 28)) { any = true; break; }
    CHECK(any);                                  // something rendered, not just clear

    // --- the viewport image composites into the UI via its texture id -------
    Image ui(220, 180);
    for (Rgba& p : ui.pixels) p = Rgba{30, 32, 38, 255};

    const ImTextureID atlasId = (ImTextureID)1;
    const ImTextureID sceneId = (ImTextureID)2;
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);

    GizmoController giz;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, 170), ImGuiCond_Always);
    vp.draw(sceneId, giz, nullptr);
    ImGui::Render();

    SoftwareImGuiRenderer renderer;
    renderer.setTexture(atlasId, { atlas, aw, ah });
    renderer.setTexture(sceneId, {
        reinterpret_cast<const unsigned char*>(vp.scene().pixels.data()),
        vp.width(), vp.height() });
    renderer.render(ImGui::GetDrawData(), ui);

    // The scene image was blitted into the panel -> green-ish terrain pixels
    // appear that are neither the desktop bg nor the panel chrome grey.
    bool greenish = false;
    for (const Rgba& p : ui.pixels)
        if (p.g > p.r + 15 && p.g > p.b + 15) { greenish = true; break; }
    CHECK(greenish);

    // --- click-picking: a centre click selects the entity straight ahead ----
    ViewportPanel pick(200, 200);
    pick.camera.eye = {0,0,0}; pick.camera.yaw = 0; pick.camera.pitch = 0;  // look +X

    WorldView wv;
    EntityState e; e.guid = 0xABCD; e.kind = 0; e.pos = {25,0,0}; e.moving = true;
    wv.apply(e);
    Mesh ground;   // a vertical wall at x=60 so terrain is behind the entity
    ground.vertices = { {{60,-50,-50},{ -1,0,0}}, {{60,50,-50},{-1,0,0}},
                        {{60,50,50},{-1,0,0}},     {{60,-50,50},{-1,0,0}} };
    ground.indices = { 0,1,2, 0,2,3 };

    // Centre pixel -> ray along +X -> hits the entity (nearer than the wall).
    PickResult pr = pick.pickAt(100, 100, wv, ground);
    CHECK(pr.hit() && pr.kind == PickResult::Kind::Entity && pr.guid == 0xABCD);

    // A pixel near the top edge aims above the entity -> the wall (terrain).
    PickResult pr2 = pick.pickAt(100, 2, wv, ground);
    CHECK(pr2.kind == PickResult::Kind::Terrain);
}
