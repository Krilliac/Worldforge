// Runnable WorldForge editor shell: a GLFW + OpenGL3 window hosting the ImGui
// panels. Built only with -DWFORGE_EDITOR_APP=ON (needs a GL/GLFW toolchain +
// display), so it is NOT part of headless CI. The panels themselves and their
// op-building logic are unit-tested separately (wforge-editor-tests).
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>

#include "editor/AtmospherePanel.hpp"
#include "editor/DebugVisPanel.hpp"
#include "debugdraw.hpp"

int main() {
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

    wf::editor::AtmospherePanel atmosphere;
    wf::editor::DebugVisPanel   debugVis;
    wf::DebugDraw               debug;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window dock space so the panels split/dock Unity/Unreal-style.
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Editor packets produced this frame would be sent over the bridge to
        // the running mangos-zero (see integration/mangos-zero/).
        std::vector<std::vector<uint8_t>> outgoing;
        atmosphere.draw(outgoing);
        debugVis.draw(debug);
        // TODO: for (auto& pkt : outgoing) bridge.send(pkt);

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
