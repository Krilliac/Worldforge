// Headless editor: render the whole editor on the CPU -- the 3D scene via the
// software rasteriser and the ImGui UI via the software backend -- and write a
// PNG. This is WorldForge's NullRHI/llvmpipe fallback: the editor "runs" and is
// screenshotted with no GPU or display. Proof the CPU path composites scene +
// chrome end to end.
#include <cmath>
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "editor/AtmospherePanel.hpp"
#include "editor/DebugVisPanel.hpp"
#include "editor/SoftwareImGui.hpp"

#include "raster.hpp"
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

    // --- 3D scene on the CPU rasteriser ------------------------------------
    Framebuffer fb(W, H);
    fb.clear(Rgba{ 18, 20, 28, 255 });

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
    Vec3 center{span*0.5f, span*0.5f, 8.0f};
    Mat4 view = Mat4::lookAt(center + Vec3{-180,-180,150}, center, {0,0,1});
    Mat4 proj = Mat4::perspective(55.0, double(W)/H, 1.0, 3000.0);
    Mat4 mvp = proj * view;
    rasterMesh(fb, mesh, mvp, Vec3{0.5f,0.4f,0.8f});

    DebugDraw dd;
    std::vector<Vec3> path;
    for (int k=0;k<=8;++k){ float x=40+k*28.f, y=150+50*std::sin(k*0.7f); path.push_back({x,y,heightAt(x,y)+2}); }
    dd.path(path, Rgba{255,220,60,255}, DebugCategory::Waypoint, true);
    dd.aabb({200,60,heightAt(200,60)}, {245,105,heightAt(222,82)+35}, Rgba{80,255,140,255}, DebugCategory::Trigger);
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb, dd, mvp, opt);

    Image img = fb.color;   // the viewport is now the background

    // --- ImGui UI on the CPU -----------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = 1.0f/60.0f;
    ImGui::StyleColorsDark();
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    editor::AtmospherePanel atmosphere;
    editor::DebugVisPanel    debugVis;

    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar()) {
        for (const char* m : { "World", "Terrain", "Objects", "Server", "Help" })
            if (ImGui::BeginMenu(m)) ImGui::EndMenu();
        ImGui::EndMainMenuBar();
    }
    std::vector<std::vector<uint8_t>> outgoing;
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_Always);
    atmosphere.draw(outgoing);
    ImGui::SetNextWindowPos(ImVec2(W - 300, 40), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, 360), ImGuiCond_Always);
    debugVis.draw(dd);
    ImGui::Render();

    editor::renderImGuiSoftware(ImGui::GetDrawData(), img, atlas, aw, ah);
    ImGui::DestroyContext();

    const char* out = "worldforge_editor.png";
    if (!writePng(img, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (%dx%d, CPU scene + CPU ImGui)\n", out, W, H);
    return 0;
}
