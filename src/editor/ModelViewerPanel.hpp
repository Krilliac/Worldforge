#pragma once
// ---------------------------------------------------------------------------
// ModelViewerPanel: a WoW-Model-Viewer-style panel -- an orbit camera around a
// single M2 with animation playback (sequence select, play/pause, time slider).
// Each frame it samples the chosen animation, skins the model (m2_render), and
// rasterises it textured into an internal Image shown in the panel. CPU-only, so
// it runs and screenshots headlessly.
// ---------------------------------------------------------------------------
#include <cmath>

#include "imgui.h"

#include "image.hpp"
#include "math.hpp"
#include "raster.hpp"      // Framebuffer
#include "m2.hpp"
#include "m2_render.hpp"

namespace wf::editor {

class ModelViewerPanel {
public:
    ModelViewerPanel(int w = 640, int h = 480)
        : width_(w), height_(h), image_(w, h), fb_(w, h) {}

    void setModel(const M2Model& model, const M2Animation& anim, const Image& texture);

    // Advance playback (ms) and re-pose; call once per frame before draw().
    void update(float dtMs);

    // ImGui panel: the rendered image (via `sceneTex`) + playback/orbit controls.
    void draw(ImTextureID sceneTex);

    const Image& image() const { return image_; }
    int width()  const { return width_; }
    int height() const { return height_; }

    // Orbit + playback state (the widgets read/write these; tests may set them).
    float azimuth = 0.8f, elevation = 0.35f, distance = 6.0f;
    Vec3  target{0, 0, 1};
    int   sequence = 0;
    float timeMs = 0.0f;
    bool  playing = true;

private:
    void render();
    Mat4 viewMatrix() const;

    int   width_, height_;
    Image image_;
    Framebuffer fb_;
    M2Model     model_;
    M2Animation anim_;
    Image       texture_;
    bool        hasModel_ = false;
};

} // namespace wf::editor
