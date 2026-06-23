#include "test.hpp"
#include "imgui.h"

#include "editor/SoftwareImGui.hpp"
#include "editor/AtmospherePanel.hpp"
#include "image.hpp"

#include <vector>

using namespace wf;
using namespace wf::editor;

void test_software_imgui() {
    std::printf("[editor.software_imgui]\n");

    // The shared headless context (test_editor_main) already built the atlas.
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* atlas = nullptr; int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    CHECK(atlas != nullptr && aw > 0 && ah > 0);

    // Render a panel frame and rasterise it onto a black image.
    Image img(640, 480);
    for (Rgba& p : img.pixels) p = Rgba{0, 0, 0, 255};

    AtmospherePanel ap;
    std::vector<std::vector<uint8_t>> out;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_Always);
    ap.draw(out);
    ImGui::Render();

    ImDrawData* dd = ImGui::GetDrawData();
    CHECK(dd != nullptr && dd->Valid && dd->CmdListsCount > 0);

    renderImGuiSoftware(dd, img, atlas, aw, ah);

    // The panel window covers part of the image -> some pixels are no longer
    // pure black (the dark window body + lighter chrome were drawn).
    int lit = 0;
    for (const Rgba& p : img.pixels)
        if (p.r > 0 || p.g > 0 || p.b > 0) ++lit;
    CHECK(lit > 1000);                       // a window's worth of pixels drawn

    // A pixel far outside the window stays black (clip rects honoured).
    CHECK(img.at(600, 460).r == 0 && img.at(600, 460).g == 0);

    // A pixel inside the window region was painted.
    bool insideLit = false;
    for (int y = 60; y < 380 && !insideLit; ++y)
        for (int x = 60; x < 320 && !insideLit; ++x) {
            const Rgba& p = img.at(x, y);
            if (p.r > 0 || p.g > 0 || p.b > 0) insideLit = true;
        }
    CHECK(insideLit);
}
