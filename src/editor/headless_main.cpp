// Headless editor: render the whole editor on the CPU -- the embedded 3D
// viewport via the software rasteriser and the ImGui UI via the software backend
// -- and write a PNG. WorldForge's NullRHI/llvmpipe fallback: the editor runs
// and is screenshotted with no GPU or display. The scene is shown INSIDE a
// Viewport panel (with a gizmo over a selected object), flanked by the
// Atmosphere and Debug Visualisation panels.
#include <cmath>
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "editor/AtmospherePanel.hpp"
#include "editor/DebugVisPanel.hpp"
#include "editor/ViewportPanel.hpp"
#include "editor/EntityInspectorPanel.hpp"
#include "editor/SoftwareImGui.hpp"

#include "server/world_sim.hpp"
#include "world_view.hpp"
#include "debugdraw.hpp"
#include "image.hpp"
#include "math.hpp"

using namespace wf;

static float heightAt(float x, float y) {
    return 26.0f * std::sin(x * 0.02f) * std::cos(y * 0.017f)
         + 10.0f * std::sin((x + y) * 0.04f);
}

int main() {
    const int W = 1280, H = 720;
    const ImTextureID kAtlasTex = (ImTextureID)1;
    const ImTextureID kSceneTex = (ImTextureID)2;

    // --- terrain mesh + debug overlay --------------------------------------
    const int G = 90; const float span = 300.0f; const float step = span / (G - 1);
    Mesh mesh; mesh.vertices.resize(size_t(G) * G);
    for (int j = 0; j < G; ++j) for (int i = 0; i < G; ++i) {
        float x = i*step, y = j*step, h = heightAt(x,y);
        float hx = heightAt(x+step,y)-heightAt(x-step,y), hy = heightAt(x,y+step)-heightAt(x,y-step);
        mesh.vertices[j*G+i] = { Vec3{x,y,h}, normalize(Vec3{-hx,-hy,2*step}) };
    }
    for (int j = 0; j < G-1; ++j) for (int i = 0; i < G-1; ++i) {
        uint32_t a=j*G+i, b=j*G+i+1, c=(j+1)*G+i, d=(j+1)*G+i+1;
        mesh.indices.insert(mesh.indices.end(), {a,b,c,b,d,c});
    }
    DebugDraw dd;
    std::vector<Vec3> path;
    for (int k=0;k<=8;++k){ float x=40+k*28.f, y=150+50*std::sin(k*0.7f); path.push_back({x,y,heightAt(x,y)+2}); }
    dd.path(path, Rgba{255,220,60,255}, DebugCategory::Waypoint, true);
    dd.aabb({200,60,heightAt(200,60)}, {245,105,heightAt(222,82)+35}, Rgba{80,255,140,255}, DebugCategory::Trigger);

    // --- a live world: NPCs + a player patrolling, mirrored into a WorldView --
    // We run the same WorldSim the bridge server runs, tick it forward, then feed
    // its snapshot into the engine's WorldView -- exactly the runtime-data path,
    // minus the socket. The view draws every entity as a moving marker and the
    // inspector lists them.
    auto place = [&](float x, float y){ return Vec3{ x, y, heightAt(x,y) + 2.0f }; };
    WorldSim sim;
    struct Npc { uint32_t entry; std::vector<Vec3> wp; } npcs[] = {
        { 299, { place(40,150),  place(120,160), place(120,90)  } },
        { 300, { place(180,200), place(240,210), place(210,260) } },
        { 301, { place(70,60),   place(60,120),  place(110,110) } },
        { 302, { place(250,120), place(260,180), place(200,150) } },
    };
    for (const Npc& n : npcs) { uint64_t g = sim.spawnCreature(n.entry, 0, n.wp[0], 0); sim.setWaypoints(g, n.wp); }
    uint64_t player = sim.spawnPlayer(0, place(150,150), 0, "Worldforge");
    sim.setWaypoints(player, { place(150,150), place(150,240), place(220,240) });
    for (int i = 0; i < 27; ++i) sim.tick(0.2f);   // let them spread along the paths

    WorldView view;
    for (const SimObject& o : sim.snapshot()) {
        EntityState e;
        e.guid = o.guid; e.kind = static_cast<uint8_t>(o.kind);
        e.entry = o.entry; e.mapId = o.mapId; e.pos = o.pos;
        e.orientation = o.orientation; e.moving = o.moving; e.speed = o.speed; e.name = o.name;
        view.apply(e, static_cast<uint32_t>(sim.simTimeMs()));
    }
    view.select(player);
    view.buildDebug(dd);                            // moving NPC/player markers

    // --- ImGui (CPU) -------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = 1.0f/60.0f;
    ImGui::StyleColorsDark();
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    io.Fonts->SetTexID(kAtlasTex);

    editor::AtmospherePanel       atmosphere;
    editor::DebugVisPanel         debugVis;
    editor::EntityInspectorPanel  inspector;
    editor::ViewportPanel         viewport(820, 540);
    editor::GizmoController        giz;
    giz.op = editor::GizmoController::Op::Translate;

    // A selected doodad sitting on the terrain -> the gizmo draws over it.
    Mat4 selected = Mat4::translate(Vec3{ 150.0f, 150.0f, heightAt(150,150) + 4.0f });

    viewport.render(mesh, dd);

    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar()) {
        for (const char* m : { "World", "Terrain", "Objects", "Server", "Help" })
            if (ImGui::BeginMenu(m)) ImGui::EndMenu();
        ImGui::EndMainMenuBar();
    }
    std::vector<std::vector<uint8_t>> outgoing;
    ImGui::SetNextWindowPos(ImVec2(0, 24), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, H - 24), ImGuiCond_Always);
    atmosphere.draw(outgoing);

    ImGui::SetNextWindowPos(ImVec2(308, 24), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(848, 580), ImGuiCond_Always);
    // Passing the live view + terrain enables click-to-select in the viewport.
    viewport.draw(kSceneTex, giz, &selected, &view, &mesh);

    const float rightH = (H - 24) * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(W - 280, 24), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, rightH), ImGuiCond_Always);
    debugVis.draw(dd);

    // The runtime-data inspector: the live entity list + selected detail.
    ImGui::SetNextWindowPos(ImVec2(W - 280, 24 + rightH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, rightH), ImGuiCond_Always);
    inspector.draw(view);
    ImGui::Render();

    // Composite: dark desktop background, then ImGui (which samples the atlas
    // for chrome and the scene texture for the viewport image).
    Image img(W, H);
    for (Rgba& p : img.pixels) p = Rgba{ 30, 32, 38, 255 };

    editor::SoftwareImGuiRenderer renderer;
    renderer.setTexture(kAtlasTex, { atlas, aw, ah });
    renderer.setTexture(kSceneTex, {
        reinterpret_cast<const unsigned char*>(viewport.scene().pixels.data()),
        viewport.width(), viewport.height() });
    renderer.render(ImGui::GetDrawData(), img);
    ImGui::DestroyContext();

    const char* out = "worldforge_editor.png";
    if (!writePng(img, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (%dx%d, embedded CPU viewport + CPU ImGui)\n", out, W, H);
    return 0;
}
