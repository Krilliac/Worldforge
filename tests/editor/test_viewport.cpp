#include "test.hpp"
#include "imgui.h"

#include "editor/Camera.hpp"
#include "editor/ViewportPanel.hpp"
#include "editor/SoftwareImGui.hpp"
#include "editor/GizmoController.hpp"
#include "world_view.hpp"
#include "picking.hpp"
#include "terrain.hpp"
#include "terrain_render.hpp"   // TerrainLayer (TileScene render overload test)
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

    // --- unified pickWorldAt: entities + the loaded tile, nearest wins -------
    TileScene scene;
    TexMesh wall;   // a wall at x=60 as a terrain chunk
    wall.vertices = { { {60,-50,-50},{-1,0,0},{0,0} }, { {60,50,-50},{-1,0,0},{0,0} },
                      { {60,50,50},{-1,0,0},{0,0} },   { {60,-50,50},{-1,0,0},{0,0} } };
    wall.indices = { 0,1,2, 0,2,3 };
    scene.terrain.chunkMeshes.push_back(wall);

    // Centre click: the entity at x=25 is nearer than the wall at x=60.
    WorldPick wp = pick.pickWorldAt(100, 100, wv, scene);
    CHECK(wp.isEntity() && wp.guid == 0xABCD);

    // With no entities, the same click resolves to the terrain wall.
    WorldView none;
    WorldPick wt = pick.pickWorldAt(100, 100, none, scene);
    CHECK(wt.isScene() && wt.kind == WorldPick::Kind::Terrain);

    // --- the TileScene render overload draws textured terrain ---------------
    // One green-textured chunk (a base layer, full coverage) under a top-down
    // camera must paint non-background pixels through the zone-lit tile path,
    // proving the editor viewport can show a real ADT tile, not just a Mesh.
    {
        Image green(4, 4);
        for (Rgba& p : green.pixels) p = Rgba{40, 160, 60, 255};
        TileScene tile;
        TexMesh chunk;
        chunk.vertices = { { {-8,-8,0},{0,0,1},{0,0} },
                           { { 8,-8,0},{0,0,1},{1,0} },
                           { { 0, 8,0},{0,0,1},{0.5f,1} } };
        chunk.indices = { 0, 1, 2 };
        tile.terrain.chunkMeshes.push_back(chunk);
        tile.terrain.chunkLayers.push_back({ TerrainLayer{ &green, nullptr } });

        ViewportPanel tvp(160, 120);
        tvp.camera.eye = {0, 0, 30}; tvp.camera.yaw = 0; tvp.camera.pitch = -1.55f;
        DebugDraw noOverlay;
        tvp.render(tile, noOverlay);
        bool painted = false;
        for (const Rgba& p : tvp.scene().pixels)
            if (!(p.r == 18 && p.g == 20 && p.b == 28)) { painted = true; break; }
        CHECK(painted);
    }

    // --- atmosphere backdrop (T1.3): sky gradient + distance fog -------------
    {
        DebugDraw noOverlay;
        Mesh empty;

        // Default (no atmosphere): the legacy flat clear everywhere, unchanged.
        ViewportPanel flat(64, 48);
        flat.render(empty, noOverlay);
        const Rgba& f0 = flat.scene().at(32, 0);
        const Rgba& f1 = flat.scene().at(32, 47);
        CHECK(f0.r == 18 && f0.g == 20 && f0.b == 28);
        CHECK(f1.r == 18 && f1.g == 20 && f1.b == 28);

        // With a zone fog colour set: a vertical gradient -- the horizon (bottom)
        // row is exactly the fog colour, the top row is the darker sky tint, and
        // they differ (the backdrop is no longer flat).
        ViewportPanel sky(64, 48);
        sky.setAtmosphere(true, Vec3{ 0.6f, 0.7f, 0.8f });
        sky.render(empty, noOverlay);
        const Rgba& top = sky.scene().at(32, 0);
        const Rgba& bot = sky.scene().at(32, 47);
        CHECK(bot.r == 153 && bot.g == 179 && bot.b == 204);   // fog colour * 255
        CHECK(top.r != bot.r || top.g != bot.g || top.b != bot.b);

        // Invalidating the atmosphere restores the legacy clear.
        sky.setAtmosphere(false, Vec3{});
        sky.render(empty, noOverlay);
        const Rgba& back = sky.scene().at(32, 0);
        CHECK(back.r == 18 && back.g == 20 && back.b == 28);

        // Distance fog: a flat white ground plane receding from the camera. In
        // the rendered frame the bottom rows are near ground and rows toward the
        // vertical centre are far ground (same column, same normal, same texel,
        // same shading) -- after the fog pass the far row must sit strictly
        // closer to the fog colour than the near row.
        Image white(2, 2);
        for (Rgba& p : white.pixels) p = Rgba{ 230, 230, 230, 255 };
        TileScene tile;
        TexMesh ground;
        ground.vertices = { { {  1,-100,-5},{0,0,1},{0,0} }, { {200,-100,-5},{0,0,1},{1,0} },
                            { {200, 100,-5},{0,0,1},{1,1} }, { {  1, 100,-5},{0,0,1},{0,1} } };
        ground.indices = { 0,1,2, 0,2,3 };
        tile.terrain.chunkMeshes.push_back(ground);
        tile.terrain.chunkLayers.push_back({ TerrainLayer{ &white, nullptr } });

        ViewportPanel fogged(160, 120);
        fogged.camera.eye = { 0, 0, 0 }; fogged.camera.yaw = 0; fogged.camera.pitch = 0;

        // Reference render (no atmosphere): identifies which pixels are ground
        // (anything not the flat clear colour) before fog is in play.
        fogged.render(tile, noOverlay);
        const Image ref = fogged.scene();
        auto isGround = [&](int x, int y) {
            const Rgba& p = ref.at(x, y);
            return !(p.r == 18 && p.g == 20 && p.b == 28);
        };
        // Same column: near the bottom edge = close ground; just below the
        // vertical centre = distant ground (the plane recedes toward the horizon).
        CHECK(isGround(80, 115) && isGround(80, 68));

        fogged.setAtmosphere(true, Vec3{ 1.0f, 0.2f, 0.2f });  // red fog: easy to read
        fogged.render(tile, noOverlay);
        const Rgba nearPx = fogged.scene().at(80, 115);
        const Rgba farPx  = fogged.scene().at(80, 68);
        // The white ground loses green to the red fog with distance: the far row
        // must have lost strictly more than the near row.
        CHECK(farPx.g < nearPx.g);
    }
}
