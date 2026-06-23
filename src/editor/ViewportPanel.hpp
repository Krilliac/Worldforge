#pragma once
// ---------------------------------------------------------------------------
// ViewportPanel: the embedded 3D view. It renders the scene (terrain mesh +
// debug overlay) on the CPU into an internal Image via the software rasteriser,
// then shows that image inside an ImGui panel and runs ImGuizmo over the
// selected object. The same image is what a GPU backend would blit -- here it is
// a registered software texture, so the whole viewport works headlessly.
// ---------------------------------------------------------------------------
#include "imgui.h"

#include "image.hpp"
#include "math.hpp"
#include "terrain.hpp"     // Mesh
#include "raster.hpp"      // Framebuffer
#include "debugdraw.hpp"
#include "editor/Camera.hpp"
#include "editor/GizmoController.hpp"

namespace wf::editor {

class ViewportPanel {
public:
    Camera camera;

    explicit ViewportPanel(int w = 900, int h = 560)
        : width_(w), height_(h), scene_(w, h), fb_(w, h) {}

    // Render terrain + overlay into the internal scene image at the camera.
    void render(const Mesh& terrain, const DebugDraw& dd);

    // ImGui panel: show the scene (sampled from `sceneTex`) and run the gizmo
    // over `selected` (may be null). Returns true while the gizmo is dragged.
    bool draw(ImTextureID sceneTex, GizmoController& giz, Mat4* selected);

    const Image& scene() const { return scene_; }
    int width()  const { return width_; }
    int height() const { return height_; }

private:
    int width_, height_;
    Image       scene_;
    Framebuffer fb_;
};

} // namespace wf::editor
