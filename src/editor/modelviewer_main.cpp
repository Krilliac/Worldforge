// Headless model viewer: load a procedural animated M2, pose it, and composite
// the WoW-Model-Viewer-style panel (embedded render + playback controls) to a
// PNG on the CPU. Proves the model-viewer path with no GPU.
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "editor/ModelViewerPanel.hpp"
#include "editor/SoftwareImGui.hpp"
#include "editor/demo_assets.hpp"
#include "image.hpp"

using namespace wf;

int main() {
    const int W = 760, H = 560;
    const ImTextureID kAtlas = (ImTextureID)1;
    const ImTextureID kModel = (ImTextureID)2;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = 1.0f/60.0f;
    ImGui::StyleColorsDark();
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    io.Fonts->SetTexID(kAtlas);

    editor::ModelViewerPanel viewer(560, 460);
    viewer.setModel(editor::makeDemoModel(), editor::makeDemoAnim(), editor::makeCheckerTexture());
    viewer.distance = 7.0f; viewer.azimuth = 0.9f; viewer.elevation = 0.3f;
    viewer.timeMs = 350.0f; viewer.playing = true;
    viewer.update(0.0f);                       // pose + render the model

    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar()) {
        for (const char* m : { "Model", "Animation", "View", "Help" })
            if (ImGui::BeginMenu(m)) ImGui::EndMenu();
        ImGui::EndMainMenuBar();
    }
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W - 20, H - 40), ImGuiCond_Always);
    viewer.draw(kModel);
    ImGui::Render();

    Image img(W, H);
    for (Rgba& p : img.pixels) p = Rgba{ 30, 32, 38, 255 };

    editor::SoftwareImGuiRenderer r;
    r.setTexture(kAtlas, { atlas, aw, ah });
    r.setTexture(kModel, {
        reinterpret_cast<const unsigned char*>(viewer.image().pixels.data()),
        viewer.width(), viewer.height() });
    r.render(ImGui::GetDrawData(), img);
    ImGui::DestroyContext();

    const char* out = "worldforge_model_viewer.png";
    if (!writePng(img, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (animated M2 in the model viewer)\n", out);
    return 0;
}
