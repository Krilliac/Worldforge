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
#include "editor/MapBrowserPanel.hpp"
#include "editor/AssetBrowserPanel.hpp"
#include "editor/OutlinerPanel.hpp"
#include "editor/TerrainToolPanel.hpp"
#include "editor/TexturePaintPanel.hpp"
#include "editor/MoveEmitter.hpp"
#include "editor/PlacementEditor.hpp"
#include "editor/Camera.hpp"
#include "editor/BridgeClient.hpp"
#include "editor_bridge.hpp"
#include "world_view.hpp"
#include "scene_pick.hpp"
#include "asset_loader.hpp"   // TileScene
#include "world_types.hpp"    // TileCoord, tile<->world, worldToTile
#include "tile_streamer.hpp"  // streaming ring policy
#include "wdl_mesh.hpp"       // buildWdlWorldMesh (distant LOD)
#include "frustum.hpp"        // makeFrustum / aabbVisible (tile culling)
#include "bounds.hpp"         // Aabb
#include <map>
#include "asset_catalog.hpp"  // listMaps / listModels
#include "placement_io.hpp"   // ScenePlacement / saveScene / loadScene

#include <fstream>
#include <sstream>
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
#include <algorithm>         // std::transform (Spawn-browser filter)

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
    // Rebuild the one-chunk pick scene from the current `mesh` (after a sculpt the
    // heights change, so the pick geometry must follow).
    auto rewrapProcedural = [&]() {
        tileScene.terrain.chunkMeshes.clear();
        TexMesh chunk;
        chunk.vertices.reserve(mesh.vertices.size());
        for (const Vertex& v : mesh.vertices)
            chunk.vertices.push_back(TexVertex{ v.position, v.normal, Vec2{0,0} });
        chunk.indices = mesh.indices;
        tileScene.terrain.chunkMeshes.push_back(std::move(chunk));
    };
    rewrapProcedural();
    WorldPick sceneSel;   // the currently-selected static object (if any)
    editor::MoveEmitter     mover;       // gizmo drag on a selected entity -> MoveObject
    editor::PlacementEditor placeEditor;  // gizmo drag on a static object -> PlacementEdit
    std::vector<editor::PlacementEdit> placementEdits;   // pending ADT write-backs

    editor::AtmospherePanel      atmosphere;
    editor::DebugVisPanel        debugVis;
    editor::EntityInspectorPanel inspector;
    editor::ViewportPanel        viewport(900, 560);
    editor::GizmoController       giz;
    editor::MapBrowserPanel      mapBrowser;
    editor::AssetBrowserPanel    assetBrowser;
    editor::OutlinerPanel        outliner;
    editor::TerrainToolPanel     terrainTool;
    editor::TexturePaintPanel    texturePaint;
    bool placeMode = false;          // when on, "Place" / the P key drops the active asset
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

    // Spawn catalog: resolve the client's creature/gameobject display ids to real
    // models (the engine's object->model pipeline) so the editor can place them.
    std::vector<DisplayModel> creatureModels, gameObjectModels, detailModels;
    if (mpq.archiveCount() > 0) {
        Dbc cdi  = loadDbc(mpq, "CreatureDisplayInfo");
        Dbc cmd  = loadDbc(mpq, "CreatureModelData");
        Dbc godi = loadDbc(mpq, "GameObjectDisplayInfo");
        Dbc get  = loadDbc(mpq, "GroundEffectTexture");
        Dbc ged  = loadDbc(mpq, "GroundEffectDoodad");
        creatureModels   = listCreatureModels(cdi, cmd);
        gameObjectModels = listGameObjectModels(godi);
        detailModels     = listGroundEffectModels(get, ged);
        std::printf("[editor] spawn catalog: %zu creature, %zu gameobject, %zu detail models\n",
                    creatureModels.size(), gameObjectModels.size(), detailModels.size());
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

    // A persistent AssetLoader over the mounted client, reused for the whole
    // session: (re)loading tiles the Map browser selects and placing models the
    // Asset browser selects. Safe over an empty mpq (methods return fallbacks);
    // the browsers then simply list nothing.
    AssetLoader loader(mpq);

    // Populate the browsers from the client catalog (empty without a client).
    mapBrowser.setMaps(listMaps(mpq));
    assetBrowser.setModels(listModels(mpq, ModelKind::M2), listModels(mpq, ModelKind::Wmo));

    // The currently-loaded real ADT tile (none until one is loaded). When loaded,
    // the viewport renders it instead of the procedural mesh.
    TileScene   realTile;
    bool        useTile = false;
    std::string tileMap; int tileX = 0, tileY = 0;
    std::string browseMap;                 // the map whose tiles the browser shows
    uint32_t    nextPlaceId = 0xF0000000u; // synthetic uniqueIds for placed WMOs
    int         placedCount = 0;
    std::vector<ScenePlacement> placements; // recorded placements (Save/Load Scene)

    // --- streaming multi-tile world ----------------------------------------
    // A low-res WDL horizon for the browse map + a cache of full-res neighbour
    // tiles streamed around the camera (read-only context; the active EDITABLE
    // tile stays `realTile`, so sculpt/paint/pick/export are unaffected).
    Wdl          worldWdl;                 // browse map's low-res heightfield
    Mesh         worldWdlMesh;             // its merged far mesh (built on map change)
    bool         haveWdl = false;
    TileStreamer streamer(1);              // ring radius in tiles (3x3 around the camera)
    std::map<int, TileScene> nearCache;    // streamed neighbours, keyed by tileKey(coord)
    bool         streamWorld = true;       // render neighbours + WDL horizon around the camera

    // Load map tile (x,y) into realTile, lit at the current day-tick. Shared by
    // the argv bootstrap and the Map browser's tile-grid clicks.
    auto loadTile = [&](const std::string& map, int x, int y) -> bool {
        TileScene ts = loader.buildTileScene(map, x, y);
        if (ts.terrain.empty()) { std::printf("[editor] tile %s %d,%d absent\n", map.c_str(), x, y); return false; }
        AssetLoader::applyLighting(ts, lights, mapId, x, y, dayTick);
        realTile = std::move(ts); useTile = true; tileMap = map; tileX = x; tileY = y;
        placements.clear();   // a fresh tile carries no ad-hoc placements
        std::printf("[editor] loaded tile %s %d,%d: %zu chunks, %zu doodads, %zu wmos\n",
                    map.c_str(), x, y, realTile.terrain.chunkMeshes.size(),
                    realTile.doodadCount(), realTile.wmoCount());
        return true;
    };
    // Load a map's WDT (tile-grid presence for the browser) and its WDL (the
    // distant-terrain horizon). Resets the neighbour cache for the new map.
    auto showMapTiles = [&](const std::string& map) {
        Wdt wdt; if (loader.loadWdt(map, wdt)) mapBrowser.setWdt(wdt);
        haveWdl = loader.loadWdl(map, worldWdl);
        worldWdlMesh = haveWdl ? buildWdlWorldMesh(worldWdl) : Mesh{};
        nearCache.clear();
        streamer = TileStreamer(streamer.radius());   // fresh resident set
    };

    // Place a model (.m2 doodad / .wmo) into the loaded tile at `world`, recording
    // it for Save Scene. No-op without a loaded tile. Kind is inferred from the
    // extension so Asset and Spawn selections both work.
    auto placeModel = [&](const std::string& path, Vec3 world) {
        if (!useTile || path.empty()) return;
        const bool isWmo = path.size() > 4 &&
            (path.compare(path.size() - 4, 4, ".wmo") == 0 ||
             path.compare(path.size() - 4, 4, ".WMO") == 0);
        if (isWmo) loader.placeWmo(realTile, path, world, 0.0f, nextPlaceId++);
        else       loader.placeDoodad(realTile, path, world);
        placements.push_back({ path, world, 0.0f, 1.0f, isWmo });
        ++placedCount;
    };

    // Bootstrap from argv "<DataDir> <Map> <x> <y>" / WFORGE_TILE_* if provided.
    if (mpq.archiveCount() > 0 && resolveTile(argc, argv, tileMap, tileX, tileY)) {
        showMapTiles(tileMap);
        loadTile(tileMap, tileX, tileY);
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
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, 1);

        // Edge-triggered P: place the active asset at the current terrain pick.
        static bool pWas = false;
        bool pNow = glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS;
        bool placeKey = pNow && !pWas; pWas = pNow;

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
        const LightingSample zoneSample = lights.lightingAt(lightPos, mapId, dayTick);
        sceneLight = shadeFromSample(zoneSample);
        // Backdrop + haze from the same sample: sky gradient and distance fog in
        // the zone fog colour (T1.3); invalid sample -> legacy flat clear.
        viewport.setAtmosphere(zoneSample.valid, zoneSample.fog);

        // Render the scene on the CPU and upload it to the GL texture. A loaded
        // real tile renders textured terrain + doodads + WMOs + liquid with its
        // zone light re-resolved at the current tick; otherwise the procedural mesh.
        if (useTile)
            AssetLoader::applyLighting(realTile, lights, mapId, tileX, tileY, dayTick);

        if (streamWorld && haveWdl && !browseMap.empty()) {
            // Drive the resident ring from the camera's tile. worldToTile is the
            // documented inverse of tileCornerWorld (VERIFY-FLAGGED axis mapping).
            TileCoord focus = worldToTile(viewport.camera.eye);
            focus.x = std::clamp(focus.x, 0, 63);
            focus.y = std::clamp(focus.y, 0, 63);
            TileStreamer::Plan plan = streamer.plan(focus);
            for (TileCoord c : plan.toEvict) { nearCache.erase(tileKey(c)); streamer.markEvicted(c); }
            for (TileCoord c : plan.toLoad) {
                TileScene ts = loader.buildTileScene(browseMap, c.x, c.y);
                if (!ts.terrain.empty()) {
                    AssetLoader::applyLighting(ts, lights, mapId, c.x, c.y, dayTick);
                    nearCache.emplace(tileKey(c), std::move(ts));
                }
                streamer.markLoaded(c);   // mark even if absent so we don't retry every frame
            }

            // Frustum-cull the resident neighbours into the draw list. The active
            // editable tile is always drawn from realTile (it carries live edits).
            const float aspect = float(viewport.width()) / float(viewport.height());
            const Frustum fr = makeFrustum(viewport.camera.proj(aspect) * viewport.camera.view());
            auto tileBox = [](TileCoord c) {
                const Vec3 nw = tileCornerWorld(c.x, c.y);
                return Aabb{ Vec3{ nw.x - float(TILE_SIZE), nw.y - float(TILE_SIZE), -2000.0f },
                            Vec3{ nw.x, nw.y, 2000.0f } };
            };
            std::vector<const TileScene*> nearTiles;
            if (useTile) nearTiles.push_back(&realTile);
            const int activeKey = useTile ? tileKey({ tileX, tileY }) : -1;
            for (auto& kv : nearCache) {
                if (kv.first == activeKey) continue;        // already added via realTile
                const TileCoord c{ kv.first % 64, kv.first / 64 };
                if (!aabbVisible(fr, tileBox(c))) continue;
                nearTiles.push_back(&kv.second);
            }
            viewport.render(nearTiles, worldWdlMesh, debug, sceneLight);
        } else if (useTile) {
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

        // --- main menu bar + inline toolbar -------------------------------------
        using Op = editor::GizmoController::Op;
        bool resetLayout = false;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Scene", nullptr, false, !placements.empty())) {
                    std::ofstream f("scene.wfscene");
                    f << saveScene(placements);
                    std::printf("[editor] saved %zu placements -> scene.wfscene\n", placements.size());
                }
                if (ImGui::MenuItem("Load Scene", nullptr, false, useTile)) {
                    std::ifstream f("scene.wfscene");
                    std::ostringstream ss; ss << f.rdbuf();
                    auto loaded = loadScene(ss.str());
                    for (const ScenePlacement& sp : loaded) placeModel(sp.model, sp.pos);
                    std::printf("[editor] loaded %zu placements from scene.wfscene\n", loaded.size());
                }
                // Export the sculpted tile as a patched ADT (heights only): the
                // original bytes with just the edited MCVT data overwritten.
                if (ImGui::MenuItem("Export Tile ADT", nullptr, false,
                                    useTile && realTile.hasSource)) {
                    try {
                        std::vector<uint8_t> bytes = loader.exportTileAdt(realTile);
                        if (bytes.empty()) {
                            std::printf("[editor] export failed: no source bytes for %s %d,%d\n",
                                        tileMap.c_str(), tileX, tileY);
                        } else {
                            char name[64];
                            std::snprintf(name, sizeof(name), "export_%d_%d.adt", tileX, tileY);
                            std::ofstream f(name, std::ios::binary);
                            f.write(reinterpret_cast<const char*>(bytes.data()),
                                    static_cast<std::streamsize>(bytes.size()));
                            std::printf("[editor] exported tile %s %d,%d -> %s (%zu bytes)\n",
                                        tileMap.c_str(), tileX, tileY, name, bytes.size());
                        }
                    } catch (const std::exception& e) {
                        std::printf("[editor] export error: %s\n", e.what());
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Esc")) glfwSetWindowShouldClose(win, 1);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Layout")) resetLayout = true;
                ImGui::Separator();
                ImGui::MenuItem("Stream World (LOD)", nullptr, &streamWorld);
                int ring = streamer.radius();
                if (ImGui::SliderInt("Ring radius", &ring, 0, 4)) streamer.setRadius(ring);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Gizmo:");
            if (ImGui::RadioButton("Move",   giz.op == Op::Translate)) giz.op = Op::Translate;
            ImGui::SameLine(); if (ImGui::RadioButton("Rotate", giz.op == Op::Rotate)) giz.op = Op::Rotate;
            ImGui::SameLine(); if (ImGui::RadioButton("Scale",  giz.op == Op::Scale))  giz.op = Op::Scale;
            ImGui::SameLine(); ImGui::Checkbox("Snap", &giz.snap);
            ImGui::SameLine(); ImGui::Separator();
            ImGui::SameLine(); ImGui::Checkbox("Place mode", &placeMode);
            ImGui::EndMainMenuBar();
        }

        // Default editor layout, built once when no saved layout exists in
        // imgui.ini: a scene/entity column on the left, the 3D viewport filling
        // the centre, and an authoring/inspector column on the right (Atmosphere
        // over Debug Visualisation). It is docked into the main viewport's
        // dockspace, so the whole layout tracks the OS window on resize / maximise
        // / restore. User rearrangements are persisted to imgui.ini and take
        // precedence on the next launch (delete imgui.ini to get this default back).
        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        ImGuiID dockspace_id = ImGui::GetID("WorldForgeDockspace");
        if (resetLayout) ImGui::DockBuilderRemoveNode(dockspace_id);  // View > Reset Layout
        if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, mainVp->Size);

            ImGuiID center = dockspace_id;
            ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.22f, nullptr, &center);
            ImGuiID leftB  = ImGui::DockBuilderSplitNode(left,   ImGuiDir_Down,  0.55f, nullptr, &left);
            ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
            ImGuiID rightB = ImGui::DockBuilderSplitNode(right,  ImGuiDir_Down,  0.45f, nullptr, &right);

            ImGui::DockBuilderDockWindow("Outliner",            left);
            ImGui::DockBuilderDockWindow("Entities",            left);
            ImGui::DockBuilderDockWindow("Map Browser",         leftB);
            ImGui::DockBuilderDockWindow("Assets",              leftB);
            ImGui::DockBuilderDockWindow("Spawn",               leftB);
            ImGui::DockBuilderDockWindow("Placement",           leftB);
            ImGui::DockBuilderDockWindow("Viewport",            center);
            ImGui::DockBuilderDockWindow("Sky",                 right);
            ImGui::DockBuilderDockWindow("Atmosphere",          right);
            ImGui::DockBuilderDockWindow("Terrain Tool",        right);
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

        // Pick against the real tile when one is loaded (so doodads/WMOs are
        // selectable), else the procedural one-chunk scene.
        const TileScene* activeScene = useTile ? &realTile : &tileScene;

        // Map browser: select a map (loads its WDT tile grid), then click a present
        // tile to load it into the viewport (takes effect next frame).
        mapBrowser.draw();
        { std::string m; if (mapBrowser.takeMapChanged(m)) { browseMap = m; showMapTiles(m); } }
        { int tx, ty; if (!browseMap.empty() && mapBrowser.takeTileRequest(tx, ty)) loadTile(browseMap, tx, ty); }

        // Asset browser: choose the active placement model.
        assetBrowser.draw();

        // Spawn browser: place creatures/objects by display id -- selecting one
        // sets the active asset to its resolved real model (.m2 / .wmo).
        {
            ImGui::Begin("Spawn");
            static char spawnFilter[128] = {0};
            ImGui::InputText("Filter##spawn", spawnFilter, sizeof spawnFilter);
            auto matches = [&](const std::string& label) {
                if (spawnFilter[0] == 0) return true;
                std::string l = label, f = spawnFilter;
                std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c){ return (char)std::tolower(c); });
                std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c){ return (char)std::tolower(c); });
                return l.find(f) != std::string::npos;
            };
            auto listTab = [&](const char* name, const std::vector<DisplayModel>& models) {
                if (!ImGui::BeginTabItem(name)) return;
                ImGui::BeginChild(name, ImVec2(0, 0));
                for (const DisplayModel& dm : models) {
                    if (!matches(dm.label)) continue;
                    if (ImGui::Selectable(dm.label.c_str(), assetBrowser.selectedPath() == dm.model))
                        assetBrowser.select(dm.model);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            };
            if (creatureModels.empty() && gameObjectModels.empty() && detailModels.empty()) {
                ImGui::TextDisabled("No display DBCs (mount a client).");
            } else if (ImGui::BeginTabBar("spawnTabs")) {
                listTab("Creatures", creatureModels);
                listTab("Objects",   gameObjectModels);
                listTab("Detail",    detailModels);
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        // Outliner: clicking an item selects that entity / scene object.
        if (auto sel = outliner.draw(view, activeScene);
            sel.kind != editor::OutlinerSelection::Kind::None) {
            using K = editor::OutlinerSelection::Kind;
            if (sel.kind == K::Entity) { view.select(sel.guid); sceneSel = WorldPick{}; }
            else if (sel.kind == K::Doodad && activeScene && sel.index < activeScene->instances.size()) {
                const Mat4& t = activeScene->instances[sel.index].transform;
                sceneSel = WorldPick{}; sceneSel.kind = WorldPick::Kind::Doodad; sceneSel.index = sel.index;
                sceneSel.point = { t.at(0,3), t.at(1,3), t.at(2,3) }; view.select(0);
            } else if (sel.kind == K::Wmo && activeScene && sel.index < activeScene->wmoInstances.size()) {
                const Mat4& t = activeScene->wmoInstances[sel.index].transform;
                sceneSel = WorldPick{}; sceneSel.kind = WorldPick::Kind::Wmo; sceneSel.index = sel.index;
                sceneSel.uniqueId = static_cast<uint32_t>(sel.guid);
                sceneSel.point = { t.at(0,3), t.at(1,3), t.at(2,3) }; view.select(0);
            }
        }

        terrainTool.draw();
        texturePaint.draw();

        // Unified click-to-select in the viewport: a left-click picks the nearest
        // of the live entities and the loaded tile -- an entity sets the WorldView
        // selection, a static object (terrain/doodad/WMO) goes to sceneSel.
        bool gizmoActive = viewport.draw((ImTextureID)(intptr_t)sceneTex, giz, &selected,
                                         &view, &mesh, activeScene, &sceneSel);

        // Terrain sculpt: while the Terrain Tool is enabled, holding the left mouse
        // over picked terrain raises/lowers/flattens under the last pick. On a real
        // loaded tile the brush edits the source MCNK height grids (MCVT) and
        // re-meshes the tile, so the edit lives in the export-ready data model; on
        // the procedural fallback it edits the in-memory mesh and re-wraps it. Both
        // keep the pick geometry in sync with the new heights.
        if (terrainTool.enabled() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            sceneSel.kind == WorldPick::Kind::Terrain) {
            if (useTile) {
                if (realTile.hasSource &&
                    terrainTool.applyChunks(realTile.sourceChunks, tileX, tileY,
                                            sceneSel.point) > 0) {
                    // Refresh MCNR normals from the new heights so shading tracks
                    // the sculpt (seamless across chunk borders), then re-mesh.
                    recomputeTileNormals(realTile.sourceChunks);
                    loader.rebuildTileTerrain(realTile);
                }
            } else if (terrainTool.apply(mesh, sceneSel.point) > 0) {
                rewrapProcedural();
            }
        }

        // Texture paint: holding the left mouse over picked terrain paints/erases
        // the selected blend layer's coverage under the cursor, straight into the
        // loaded tile's render AlphaMaps (the splat rasteriser samples them, so it
        // shows next frame without a re-mesh). Real tiles only -- the procedural
        // scene has no blend layers. Suppressed while the sculpt tool is active so
        // a drag does one thing at a time.
        if (texturePaint.enabled() && !terrainTool.enabled() && useTile &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            sceneSel.kind == WorldPick::Kind::Terrain) {
            texturePaint.paint(realTile.terrain.chunkAlphas, realTile.sourceChunks,
                               tileX, tileY, sceneSel.point);
        }

        debugVis.draw(debug);
        // live entities + selected static object + spawn/despawn authoring ops
        inspector.draw(view, &sceneSel, &outgoing);

        // Placement: drop the Asset browser's active model at the current terrain
        // pick point. Enable Place mode, click the ground to choose a spot, then
        // press P or the button. Needs a loaded real tile so the object renders.
        {
            ImGui::Begin("Placement");
            if (assetBrowser.hasSelection())
                ImGui::TextWrapped("Active: %s", assetBrowser.selectedPath().c_str());
            else
                ImGui::TextDisabled("Pick a model in the Assets panel.");
            const bool terrainSel = sceneSel.kind == WorldPick::Kind::Terrain;
            const bool canPlace = placeMode && useTile && assetBrowser.hasSelection() && terrainSel;
            ImGui::BeginDisabled(!canPlace);
            bool placeBtn = ImGui::Button("Place at selection (P)");
            ImGui::EndDisabled();
            if (!placeMode)        ImGui::TextDisabled("Enable 'Place mode' in the menu bar.");
            else if (!useTile)     ImGui::TextDisabled("Load a tile (Map Browser) first.");
            else if (!terrainSel)  ImGui::TextDisabled("Click the ground to choose a spot.");
            if ((placeBtn || placeKey) && canPlace)
                placeModel(assetBrowser.selectedPath(), sceneSel.point);
            ImGui::Text("Placed: %d (%zu saved)", placedCount, placements.size());
            ImGui::End();
        }

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

        // --- status bar pinned to the bottom of the main viewport ---------------
        {
            const float barH = ImGui::GetFrameHeight();
            ImGui::SetNextWindowPos(ImVec2(mainVp->WorkPos.x,
                                           mainVp->WorkPos.y + mainVp->WorkSize.y - barH));
            ImGui::SetNextWindowSize(ImVec2(mainVp->WorkSize.x, barH));
            ImGuiWindowFlags sf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                  ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
            if (ImGui::Begin("##statusbar", nullptr, sf)) {
                const Vec3 eye = viewport.camera.eye;
                ImGui::Text("cam (%.0f, %.0f, %.0f)", eye.x, eye.y, eye.z);
                ImGui::SameLine(0, 24);
                if (useTile) ImGui::Text("tile %s %d,%d", tileMap.c_str(), tileX, tileY);
                else         ImGui::TextUnformatted("tile (procedural)");
                ImGui::SameLine(0, 24);
                if (view.hasSelection())
                    ImGui::Text("sel entity 0x%llX", (unsigned long long)view.selected());
                else if (sceneSel.kind == WorldPick::Kind::Terrain) ImGui::TextUnformatted("sel terrain");
                else if (sceneSel.kind == WorldPick::Kind::Doodad)  ImGui::TextUnformatted("sel doodad");
                else if (sceneSel.kind == WorldPick::Kind::Wmo)     ImGui::TextUnformatted("sel wmo");
                else ImGui::TextUnformatted("sel none");
                ImGui::SameLine(0, 24);
                ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
            }
            ImGui::End();
        }

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
