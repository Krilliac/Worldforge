#include "test.hpp"
#include "imgui.h"

#include "editor/ModelViewerPanel.hpp"
#include "editor/SoftwareImGui.hpp"
#include "editor/demo_assets.hpp"
#include "image.hpp"

using namespace wf;
using namespace wf::editor;

void test_model_viewer() {
    std::printf("[editor.model_viewer]\n");

    ModelViewerPanel viewer(200, 160);
    viewer.setModel(makeDemoModel(), makeDemoAnim(), makeCheckerTexture());
    viewer.distance = 7.0f;

    // --- animation poses differ over time -> the model actually moves -------
    viewer.playing = false;
    viewer.timeMs = 0.0f; viewer.update(0.0f);
    // Re-pose at two times and compare the top-front vertex world position.
    M2Model m = makeDemoModel();
    M2Animation a = makeDemoAnim();
    TexMesh at0   = poseM2(m, a, 0, 0);
    TexMesh at500 = poseM2(m, a, 0, 500);
    bool moved = false;
    for (size_t i = 0; i < at0.vertices.size(); ++i) {
        Vec3 d = at0.vertices[i].position - at500.vertices[i].position;
        if (length(d) > 0.05f) { moved = true; break; }
    }
    CHECK(moved);                                  // the sway bone animated the top

    // --- the viewer renders a non-empty image -------------------------------
    viewer.playing = true; viewer.timeMs = 300.0f;
    viewer.update(0.0f);                           // re-pose + render
    bool any = false;
    for (const Rgba& p : viewer.image().pixels)
        if (!(p.r==28 && p.g==30 && p.b==38)) { any = true; break; }
    CHECK(any);                                    // model rasterised over the clear

    // --- the ImGui panel draws (controls + the embedded image) --------------
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);

    const ImTextureID atlasId = (ImTextureID)1;
    const ImTextureID modelId = (ImTextureID)2;
    Image ui(280, 320);
    for (Rgba& p : ui.pixels) p = Rgba{30,32,38,255};

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_Always);
    viewer.draw(modelId);
    ImGui::Render();

    SoftwareImGuiRenderer r;
    r.setTexture(atlasId, { atlas, aw, ah });
    r.setTexture(modelId, { reinterpret_cast<const unsigned char*>(viewer.image().pixels.data()),
                            viewer.width(), viewer.height() });
    r.render(ImGui::GetDrawData(), ui);
    CHECK(ImGui::GetDrawData()->Valid);

    bool checker = false;                          // the checker texture's tan appears
    for (const Rgba& p : ui.pixels)
        if (p.r > 150 && p.g > 110 && p.g < 200 && p.b < 130) { checker = true; break; }
    CHECK(checker);
}
