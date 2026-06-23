// Headless ImGui test harness: ImGui core runs with no GPU/display -- we build
// the font atlas on the CPU, set a dummy texture id, and drive NewFrame/Render
// so panels can be exercised in CI without a window.
#include "test.hpp"
#include "imgui.h"

#include <cstdint>

void test_panels();
void test_gizmo_controller();
void test_software_imgui();
void test_viewport();
void test_bridge_client();
void test_e2e();
void test_model_viewer();
void test_entity_inspector();
void test_move_emitter();

static void setupHeadlessImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;          // no disk IO in tests
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime   = 1.0f / 60.0f;

    // Build the atlas on the CPU and hand ImGui a dummy texture id so NewFrame's
    // "fonts built" assertion passes without a renderer backend.
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));   // dummy id (no renderer)
}

int main() {
    setupHeadlessImGui();
    test_panels();
    test_gizmo_controller();
    test_software_imgui();
    test_viewport();
    test_bridge_client();
    test_e2e();
    test_model_viewer();
    test_entity_inspector();
    test_move_emitter();
    ImGui::DestroyContext();

    std::printf("\n%d checks, %d failures\n", test::g_checks, test::g_failures);
    return test::g_failures == 0 ? 0 : 1;
}
