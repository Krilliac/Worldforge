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
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include "editor/AtmospherePanel.hpp"
#include "editor/DebugVisPanel.hpp"
#include "editor/ViewportPanel.hpp"
#include "editor/Camera.hpp"
#include "editor/BridgeClient.hpp"
#include "editor_bridge.hpp"
#include "debugdraw.hpp"
#include "math.hpp"

using namespace wf;

static float heightAt(float x, float y) {
    return 26.0f * std::sin(x * 0.02f) * std::cos(y * 0.017f)
         + 10.0f * std::sin((x + y) * 0.04f);
}

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
    std::vector<Vec3> path;
    for (int k=0;k<=8;++k){ float x=40+k*28.f, y=150+50*std::sin(k*0.7f); path.push_back({x,y,heightAt(x,y)+2}); }
    debug.path(path, Rgba{255,220,60,255}, DebugCategory::Waypoint, true);

    editor::AtmospherePanel atmosphere;
    editor::DebugVisPanel    debugVis;
    editor::ViewportPanel    viewport(900, 560);
    editor::GizmoController  giz;
    Mat4 selected = Mat4::translate(Vec3{150, 150, heightAt(150,150) + 4});

    // Connect to the running mangos-zero bridge (optional; the editor still runs
    // offline if it's down). When connected, panel ops go out and the server's
    // .debug vis stream comes back into the overlay.
    editor::BridgeClient bridge;
    bridge.connect("127.0.0.1", 7878);

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

        // Render the scene on the CPU and upload it to the GL texture.
        viewport.render(mesh, debug);
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewport.width(), viewport.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, viewport.scene().pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        std::vector<std::vector<uint8_t>> outgoing;
        atmosphere.draw(outgoing);
        viewport.draw((ImTextureID)(intptr_t)sceneTex, giz, &selected);
        debugVis.draw(debug);

        // Ship the panels' ops; fold the server's debug stream into the overlay.
        for (auto& pkt : outgoing) bridge.send(pkt);
        for (const auto& f : bridge.poll()) {
            switch (f.opcode) {
                case EDITOR_DEBUG_LINE:   apply(debug, decodeDebugLine(f.payload));   break;
                case EDITOR_DEBUG_PATH:   apply(debug, decodeDebugPath(f.payload));   break;
                case EDITOR_DEBUG_VOLUME: apply(debug, decodeDebugVolume(f.payload)); break;
                case EDITOR_DEBUG_MARKER: apply(debug, decodeDebugMarker(f.payload)); break;
                case EDITOR_DEBUG_CLEAR:  debug.clear(); break;
                default: break;
            }
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
