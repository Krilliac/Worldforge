// Runnable WorldForge editor shell: a GLFW + OpenGL3 window hosting the ImGui
// panels and the embedded 3D viewport. Built only with -DWFORGE_EDITOR_APP=ON
// (needs a GL/GLFW toolchain + display), so it is NOT part of headless CI -- the
// panels, camera, viewport, and software-render fallback are unit-tested
// separately (wforge-editor-tests / wforge-editor-headless).
//
// The scene is rendered on the CPU (ViewportPanel, the same path the headless
// build uses) and uploaded to a GL texture each frame, so the desktop and
// headless editors are pixel-identical in the viewport.
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder* API for the default layout
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include "editor/AtmospherePanel.hpp"
#include "editor/DebugVisPanel.hpp"
#include "editor/ViewportPanel.hpp"
#include "editor/EntityInspectorPanel.hpp"
#include "editor/MoveEmitter.hpp"
#include "editor/PlacementEditor.hpp"
#include "editor/Camera.hpp"
#include "editor/BridgeClient.hpp"
#include "editor_bridge.hpp"
#include "world_view.hpp"
#include "scene_pick.hpp"
#include "asset_loader.hpp"   // TileScene
#include "raster.hpp"         // TexMesh / ShadeLight
#include "debugdraw.hpp"
#include "math.hpp"
#include "mpq.hpp"            // MpqManager
#include "client_data.hpp"   // findDataDir / detectLocale / mountWowClient
#include "lighting.hpp"      // LightDatabase / LightingSample / kNoonTick
#include "wow_files.hpp"     // Dbc

#include <filesystem>
#include <cstdlib>           // std::getenv
#include <string>

using namespace wf;

static float heightAt(float x, float y) {
    return 26.0f * std::sin(x * 0.02f) * std::cos(y * 0.017f)
         + 10.0f * std::sin((x + y) * 0.04f);
}

namespace {
// Parse DBFilesClient\<name>.dbc from the mounted chain; empty Dbc if absent.
Dbc loadDbc(const MpqManager& mpq, const std::string& name) {
    std::vector<uint8_t> buf; Dbc d;
    if (mpq.readFile("DBFilesClient\\" + name + ".dbc", buf)) {
        try { d = Dbc::parse(buf); } catch (...) {}
    }
    return d;
}

// LightingSample -> ShadeLight: adopt the zone ambient/diffuse, keep the standard
// sun direction. An invalid sample leaves the legacy grey defaults untouched.
ShadeLight shadeFromSample(const LightingSample& s) {
    ShadeLight sl;
    if (s.valid) { sl.ambient = s.ambient; sl.diffuse = s.diffuse; }
    return sl;
}

// Resolve the client Data dir: explicit argv[1] or $WFORGE_CLIENT first, then
// auto-discovery walking up from there (or the cwd).
std::filesystem::path resolveDataDir(int argc, char** argv) {
    std::string hint;
    if (argc > 1) hint = argv[1];
    else if (const char* e = std::getenv("WFORGE_CLIENT")) hint = e;
    std::filesystem::path d = findDataDir(hint);
    if (d.empty() && !hint.empty()) d = hint;  // accept an explicit Data dir verbatim
    return d;
}

// Optional real-tile selector: argv "<DataDir> <Map> <x> <y>" (coords after the
// map name) or the WFORGE_TILE_MAP / WFORGE_TILE_X / WFORGE_TILE_Y env vars.
// Returns true only when a map and both block indices are present, so the editor
// falls back to procedural terrain whenever a tile wasn't requested.
bool resolveTile(int argc, char** argv, std::string& map, int& x, int& y) {
    if (argc > 4) { map = argv[2]; x = std::atoi(argv[3]); y = std::atoi(argv[4]); }
    if (const char* e = std::getenv("WFORGE_TILE_MAP")) map = e;
    if (const char* e = std::getenv("WFORGE_TILE_X"))   x = std::atoi(e);
    if (const char* e = std::getenv("WFORGE_TILE_Y"))   y = std::atoi(e);
    return !map.empty() && x >= 0 && y >= 0;
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) { std::fprintf(stderr, "glfw init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1280, 720, "WorldForge Editor", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // docking branch
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Build the terrain mesh + debug overlay once.
    const int G = 90; const float span = 300.0f; const float step = span / (G - 1);
    Mesh mesh; mesh.vertices.resize((size_t)G * G);
    for (int j = 0; j < G; ++j) for (int i = 0; i < G; ++i) {
        float x = i*step, y = j*step, h = heightAt(x,y);
        float hx = heightAt(x+step,y)-heightAt(x-step,y), hy = heightAt(x,y+step)-heightAt(x,y-step);
        mesh.vertices[j*G+i] = { Vec3{x,y,h}, normalize(Vec3{-hx,-hy,2*step}) };
    }
    for (int j = 0; j < G-1; ++j) for (int i = 0; i < G-1; ++i) {
        uint32_t a=j*G+i, b=j*G+i+1, c=(j+1)*G+i, d=(j+1)*G+i+1;
        mesh.indices.insert(mesh.indices.end(), {a,b,c,b,d,c});
    }
    DebugDraw debug;
    std::vector<Vec3> staticPath;
    for (int k=0;k<=8;++k){ float x=40+k*28.f, y=150+50*std::sin(k*0.7f); staticPath.push_back({x,y,heightAt(x,y)+2}); }

    // Wrap the terrain as a one-chunk TileScene so the viewport's unified click
    // path can pick terrain (and, once a real ADT is loaded, doodads/WMOs too).
    TileScene tileScene;
    {
        TexMesh chunk;
        chunk.vertices.reserve(mesh.vertices.size());
        for (const Vertex& v : mesh.vertices)
            chunk.vertices.push_back(TexVertex{ v.position, v.normal, Vec2{0,0} });
        chunk.indices = mesh.indices;
        tileScene.terrain.chunkMeshes.push_back(std::move(chunk));
    }
    WorldPick sceneSel;   // the currently-selected static object (if any)
    editor::MoveEmitter     mover;       // gizmo drag on a selected entity -> MoveObject
    editor::PlacementEditor placeEditor;  // gizmo drag on a static object -> PlacementEdit
    std::vector<editor::PlacementEdit> placementEdits;   // pending ADT write-backs

    editor::AtmospherePanel      atmosphere;
    editor::DebugVisPanel        debugVis;
    editor::EntityInspectorPanel inspector;
    editor::ViewportPanel        viewport(900, 560);
    editor::GizmoController       giz;
    Mat4 selected = Mat4::translate(Vec3{150, 150, heightAt(150,150) + 4});

    // The engine-side mirror of the live server world (NPCs/players) + the
    // accumulated server debug-vis stream. Rebuilt into `debug` each frame so the
    // moving entity markers stay current.
    WorldView view;
    std::vector<DebugMarker> srvMarkers; std::vector<DebugPath> srvPaths;
    std::vector<DebugLine>   srvLines;   std::vector<DebugVolume> srvVols;

    // Connect to the running mangos-zero bridge (optional; the editor still runs
    // offline if it's down). When connected, panel ops go out and the server's
    // live entity stream + .debug vis stream come back.
    editor::BridgeClient bridge;
    bridge.connect("127.0.0.1", 7878);

    // Mount the vanilla client (optional) and build the Light.dbc database so the
    // viewport shades with real zone lighting. If the client isn't found the
    // ShadeLight keeps its legacy grey defaults and nothing else changes.
    MpqManager mpq;
    LightDatabase lights;
    {
        std::filesystem::path dataDir = resolveDataDir(argc, argv);
        if (!dataDir.empty()) {
            std::string locale = detectLocale(dataDir);
            if (locale.empty()) locale = "enUS";
            mountWowClient(mpq, dataDir, locale);
            if (mpq.archiveCount() > 0) {
                Dbc light = loadDbc(mpq, "Light"),         lparams = loadDbc(mpq, "LightParams");
                Dbc lint  = loadDbc(mpq, "LightIntBand"),  lfloat  = loadDbc(mpq, "LightFloatBand");
                lights.build(&light, &lparams, &lint, &lfloat);
                std::printf("[editor] client mounted: %zu archives, Light.dbc %s\n",
                            mpq.archiveCount(), lights.empty() ? "absent" : "loaded");
            }
        }
        if (lights.empty())
            std::printf("[editor] no client/Light.dbc -- using legacy grey light\n");
    }
    // Live day-tick (T1.2): the viewport's lighting is resolved every frame from
    // the global sky at `dayTick` (0..2880, one WoW day), so dragging the Sky
    // slider re-shades the scene and the optional auto-advance animates dawn->dusk.
    // An empty LightDatabase yields the legacy grey ShadeLight regardless of tick.
    const uint32_t mapId    = 0;                          // Azeroth global sky
    const Vec3     lightPos { 150.0f, 150.0f, 0.0f };     // sample point for the zone sky
    float          dayTick  = kNoonTick;                  // current time-of-day tick
    bool           dayAuto  = false;                      // animate the day over real time
    float          dayRate  = 120.0f;                     // ticks/sec when auto (2880 = 24s/day)
    ShadeLight     sceneLight;                            // recomputed each frame, below

    // Optional: load a real ADT tile from the mounted client and show it in the
    // viewport instead of the procedural mesh (E.3's read-only-viewport milestone).
    // Requested via argv "<DataDir> <Map> <x> <y>" or WFORGE_TILE_*; absent or
    // unresolvable -> useTile stays false and the procedural terrain renders.
    TileScene   realTile;
    bool        useTile = false;
    std::string tileMap; int tileX = 0, tileY = 0;
    if (mpq.archiveCount() > 0 && resolveTile(argc, argv, tileMap, tileX, tileY)) {
        AssetLoader loader(mpq);
        realTile = loader.buildTileScene(tileMap, tileX, tileY);
        useTile  = !realTile.terrain.empty();
        if (useTile)
            std::printf("[editor] tile %s %d,%d: %zu chunks, %zu doodads, %zu wmos\n",
                        tileMap.c_str(), tileX, tileY, realTile.terrain.chunkMeshes.size(),
                        realTile.doodadCount(), realTile.wmoCount());
        else
            std::printf("[editor] tile %s %d,%d absent -- procedural terrain\n",
                        tileMap.c_str(), tileX, tileY);
    }

    GLuint sceneTex = 0;
    glGenTextures(1, &sceneTex);

    double lastT = glfwGetTime();
    double lastX = 0, lastY = 0; glfwGetCursorPos(win, &lastX, &lastY);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = float(now - lastT); lastT = now;

        // WASD fly + RMB mouse-look (only when the viewport has focus-ish).
        double mx, my; glfwGetCursorPos(win, &mx, &my);
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            viewport.camera.look(float(mx - lastX) * 0.005f, float(lastY - my) * 0.005f);
        }
        lastX = mx; lastY = my;
        float spd = 80.0f * dt;
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) viewport.camera.fly( spd, 0, 0);
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) viewport.camera.fly(-spd, 0, 0);
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) viewport.camera.fly(0,  spd, 0);
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) viewport.camera.fly(0, -spd, 0);
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) viewport.camera.fly(0, 0,  spd);
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) viewport.camera.fly(0, 0, -spd);

        // Drain the bridge: live entities/server-status feed the WorldView; the
        // debug-vis stream accumulates (cleared on DEBUG_CLEAR).
        for (const auto& f : bridge.poll()) {
            switch (f.opcode) {
                case EDITOR_ENTITY_STATE:
                case EDITOR_ENTITY_REMOVE:
                case EDITOR_SERVER_STATE:
                    view.onFrame(f, (uint32_t)(now * 1000.0)); break;
                case EDITOR_DEBUG_MARKER: srvMarkers.push_back(decodeDebugMarker(f.payload)); break;
                case EDITOR_DEBUG_PATH:   srvPaths.push_back(decodeDebugPath(f.payload));     break;
                case EDITOR_DEBUG_LINE:   srvLines.push_back(decodeDebugLine(f.payload));     break;
                case EDITOR_DEBUG_VOLUME: srvVols.push_back(decodeDebugVolume(f.payload));    break;
                case EDITOR_DEBUG_CLEAR:
                    srvMarkers.clear(); srvPaths.clear(); srvLines.clear(); srvVols.clear(); break;
                default: break;
            }
        }

        // Rebuild the overlay each frame: static authoring path + accumulated
        // server debug + the live moving entities.
        debug.clear();
        debug.path(staticPath, Rgba{255,220,60,255}, DebugCategory::Waypoint, true);
        for (const auto& m : srvMarkers) apply(debug, m);
        for (const auto& p : srvPaths)   apply(debug, p);
        for (const auto& l : srvLines)   apply(debug, l);
        for (const auto& v : srvVols)    apply(debug, v);
        view.buildDebug(debug);

        // Park the gizmo on whatever is selected: a live entity, or the hit
        // point of a picked static scene object.
        if (view.hasSelection())
            selected = Mat4::translate(view.find(view.selected())->state.pos);
        else if (sceneSel.isScene())
            selected = Mat4::translate(sceneSel.point);

        // Advance the day-tick (when animating) and re-resolve the scene light so
        // the viewport tracks the time of day. Empty Light.dbc -> grey default.
        if (dayAuto) { dayTick += dayRate * dt; dayTick = std::fmod(dayTick, kDayTicks); }
        sceneLight = shadeFromSample(lights.lightingAt(lightPos, mapId, dayTick));

        // Render the scene on the CPU and upload it to the GL texture. A loaded
        // real tile renders textured terrain + doodads + WMOs + liquid with its
        // zone light re-resolved at the current tick; otherwise the procedural mesh.
        if (useTile) {
            AssetLoader::applyLighting(realTile, lights, mapId, tileX, tileY, dayTick);
            viewport.render(realTile, debug, sceneLight);
        } else {
            viewport.render(mesh, debug, sceneLight);
        }
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewport.width(), viewport.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, viewport.scene().pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Default editor layout, built once when no saved layout exists in
        // imgui.ini: a scene/entity column on the left, the 3D viewport filling
        // the centre, and an authoring/inspector column on the right (Atmosphere
        // over Debug Visualisation). It is docked into the main viewport's
        // dockspace, so the whole layout tracks the OS window on resize / maximise
        // / restore. User rearrangements are persisted to imgui.ini and take
        // precedence on the next launch (delete imgui.ini to get this default back).
        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        ImGuiID dockspace_id = ImGui::GetID("WorldForgeDockspace");
        if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, mainVp->Size);

            ImGuiID center = dockspace_id;
            ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.20f, nullptr, &center);
            ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
            ImGuiID rightB = ImGui::DockBuilderSplitNode(right,  ImGuiDir_Down,  0.45f, nullptr, &right);

            ImGui::DockBuilderDockWindow("Entities",            left);
            ImGui::DockBuilderDockWindow("Viewport",            center);
            ImGui::DockBuilderDockWindow("Sky",                 right);
            ImGui::DockBuilderDockWindow("Atmosphere",          right);
            ImGui::DockBuilderDockWindow("Debug Visualisation", rightB);
            ImGui::DockBuilderFinish(dockspace_id);
        }
        ImGui::DockSpaceOverViewport(dockspace_id, mainVp);

        // Sky panel: drive the live day-tick that shades the viewport (T1.2).
        // The tick is a WoW half-minute clock (2880/day); show it as HH:MM and
        // let the user scrub or auto-advance it. With no Light.dbc mounted the
        // light stays grey, so flag that the slider has no visible effect.
        {
            ImGui::Begin("Sky");
            const int totalMin = int(dayTick * 0.5f);        // 2 ticks == 1 minute
            ImGui::Text("Time of day: %02d:%02d", (totalMin / 60) % 24, totalMin % 60);
            ImGui::SliderFloat("Day tick", &dayTick, 0.0f, kDayTicks, "%.0f / 2880");
            ImGui::Checkbox("Auto-advance", &dayAuto);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("ticks/s", &dayRate, 10.0f, 600.0f, "%.0f");
            if (ImGui::Button("Dawn"))  dayTick = 0.25f * kDayTicks;
            ImGui::SameLine(); if (ImGui::Button("Noon"))  dayTick = kNoonTick;
            ImGui::SameLine(); if (ImGui::Button("Dusk"))  dayTick = 0.75f * kDayTicks;
            ImGui::SameLine(); if (ImGui::Button("Night")) dayTick = 0.0f;
            ImGui::Separator();
            if (lights.empty()) {
                ImGui::TextDisabled("No Light.dbc mounted - grey light, tick has no effect.");
            } else {
                ImGui::Text("ambient %.2f %.2f %.2f", sceneLight.ambient.x, sceneLight.ambient.y, sceneLight.ambient.z);
                ImGui::Text("diffuse %.2f %.2f %.2f", sceneLight.diffuse.x, sceneLight.diffuse.y, sceneLight.diffuse.z);
            }
            ImGui::End();
        }

        std::vector<std::vector<uint8_t>> outgoing;
        atmosphere.draw(outgoing);
        // Unified click-to-select: a left-click picks the nearest of the live
        // entities and the loaded tile -- an entity sets the WorldView
        // selection, a static object (terrain/doodad/WMO) goes to sceneSel.
        // Pick against the real tile when one is loaded (so doodads/WMOs are
        // selectable), else the procedural one-chunk scene.
        const TileScene* activeScene = useTile ? &realTile : &tileScene;
        bool gizmoActive = viewport.draw((ImTextureID)(intptr_t)sceneTex, giz, &selected,
                                         &view, &mesh, activeScene, &sceneSel);
        debugVis.draw(debug);
        // live entities + selected static object + spawn/despawn authoring ops
        inspector.draw(view, &sceneSel, &outgoing);

        // Dragging the gizmo on a selected entity relocates it on the server.
        {
            const Vec3 gpos{ selected.at(0,3), selected.at(1,3), selected.at(2,3) };
            float ori = view.hasSelection() ? view.find(view.selected())->state.orientation : 0.0f;
            if (auto op = mover.update(gizmoActive, view.selected(), gpos, ori))
                bridge.send(encode(*op));
            // A static object (doodad/WMO) drag is an asset edit, captured for an
            // offline ADT write-back (no live server op for placements).
            if (auto edit = placeEditor.update(gizmoActive, sceneSel, gpos))
                placementEdits.push_back(*edit);
        }

        // Ship the panels' ops to the server.
        for (auto& pkt : outgoing) bridge.send(pkt);

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    glDeleteTextures(1, &sceneTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
