#include "editor/ViewportPanel.hpp"

namespace wf::editor {

void ViewportPanel::applyPendingResize() {
    // Realise the size recorded by draw() here (not in draw() itself) so the scene
    // image, width_/height_, and the framebuffer stay mutually consistent for the
    // whole frame: draw() only records the requested size; render() realises it.
    if (pendingW_ > 0 && pendingH_ > 0 && (pendingW_ != width_ || pendingH_ != height_)) {
        width_  = pendingW_;
        height_ = pendingH_;
        scene_  = Image(width_, height_);
        fb_     = Framebuffer(width_, height_);
    }
}

void ViewportPanel::setAtmosphere(bool valid, const Vec3& fogColorLinear) {
    skyValid_ = valid;
    if (!valid) return;
    // Horizon = the zone fog colour; top = the same colour darkened and shifted
    // toward blue so the gradient reads as sky (same mapping as the offline
    // flythrough render, so the viewport and stills match).
    auto u8 = [](float v) {
        return (uint8_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f);
    };
    skyHorizon_ = Rgba{ u8(fogColorLinear.x), u8(fogColorLinear.y), u8(fogColorLinear.z), 255 };
    skyTop_     = Rgba{ u8(fogColorLinear.x * 0.55f + 0.10f),
                        u8(fogColorLinear.y * 0.55f + 0.16f),
                        u8(fogColorLinear.z * 0.55f + 0.32f), 255 };
}

void ViewportPanel::clearBackdrop() {
    if (skyValid_) fillSkyGradient(fb_, skyTop_, skyHorizon_);
    else           fb_.clear(Rgba{ 18, 20, 28, 255 });
}

void ViewportPanel::fogPass() {
    if (skyValid_) applyDistanceFog(fb_, skyHorizon_);
}

void ViewportPanel::render(const Mesh& terrain, const DebugDraw& dd, const ShadeLight& light) {
    applyPendingResize();
    clearBackdrop();
    const float aspect = float(width_) / float(height_);
    const Mat4 mvp = camera.proj(aspect) * camera.view();
    rasterMesh(fb_, terrain, mvp, light);
    fogPass();                                   // fog geometry, not the overlay
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb_, const_cast<DebugDraw&>(dd), mvp, opt);
    scene_ = fb_.color;
}

void ViewportPanel::render(const TileScene& scene, const DebugDraw& dd, const ShadeLight& light) {
    applyPendingResize();
    clearBackdrop();
    const float aspect = float(width_) / float(height_);
    const Mat4 mvp = camera.proj(aspect) * camera.view();
    // Textured terrain + doodads + WMOs + liquid, lit by the tile's zone light;
    // renderLit() takes the sun direction and reads scene.light / liquidLight.
    scene.renderLit(fb_, mvp, light.dir);
    fogPass();
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb_, const_cast<DebugDraw&>(dd), mvp, opt);
    scene_ = fb_.color;
}

void ViewportPanel::render(const std::vector<const TileScene*>& nearTiles, const Mesh& wdlFar,
                           const DebugDraw& dd, const ShadeLight& light) {
    applyPendingResize();
    clearBackdrop();
    const float aspect = float(width_) / float(height_);
    const Mat4 mvp = camera.proj(aspect) * camera.view();
    // Coarse WDL horizon first; the depth-tested full-res near tiles then overwrite
    // it wherever they coincide (same world footprint, the real surface wins).
    if (!wdlFar.indices.empty()) rasterMesh(fb_, wdlFar, mvp, light);
    for (const TileScene* ts : nearTiles)
        if (ts) ts->renderLit(fb_, mvp, light.dir);
    fogPass();
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb_, const_cast<DebugDraw&>(dd), mvp, opt);
    scene_ = fb_.color;
}

void ViewportPanel::resize(int w, int h) {
    // w,h are the panel (display) size. The render target is the scaled-down
    // size; render() reallocates it next frame. Aspect is preserved.
    if (w < 1 || h < 1) return;
    displayW_ = w;
    displayH_ = h;
    pendingW_ = std::max(1, (int)(w * renderScale_ + 0.5f));
    pendingH_ = std::max(1, (int)(h * renderScale_ + 0.5f));
}

void ViewportPanel::setRenderScale(float s) {
    renderScale_ = s < 0.25f ? 0.25f : (s > 1.0f ? 1.0f : s);
    resize(displayW_, displayH_);   // re-apply to the render target
}

Ray ViewportPanel::rayAt(float localX, float localY) const {
    // Mouse coords are in DISPLAY pixels; the screen ray uses display size (the
    // aspect matches the render target, which is a uniform scale of it).
    const float aspect = float(displayW_) / float(displayH_);
    return screenRay(camera.eye, camera.forward(), camera.right(), camera.up(),
                     camera.fovY, aspect, localX, localY, float(displayW_), float(displayH_));
}

PickResult ViewportPanel::pickAt(float localX, float localY,
                                 const WorldView& view, const Mesh& terrain) const {
    return pick(rayAt(localX, localY), view, terrain);
}

WorldPick ViewportPanel::pickWorldAt(float localX, float localY,
                                     const WorldView& view, const TileScene& scene) const {
    return pickWorld(rayAt(localX, localY), view, scene);
}

bool ViewportPanel::draw(ImTextureID sceneTex, GizmoController& giz, Mat4* selected,
                         WorldView* view, const Mesh* terrain,
                         const TileScene* scene, WorldPick* sceneSel) {
    bool using_ = false;
    ImGui::Begin("Viewport");

    // Request a render target matching the panel's content area so the scene
    // fills the dock node with the right aspect as the window / dockspace is
    // resized. The size is applied on the next frame's render() (1-frame lag,
    // imperceptible); this frame the image draws at its current resolution.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    resize((int)avail.x, (int)avail.y);

    const ImVec2 imgPos = ImGui::GetCursorScreenPos();
    // Stretch the (possibly lower-res) render target across the full panel; the
    // GPU upsamples it (LINEAR), so the CPU rasteriser shades far fewer pixels.
    ImGui::Image(sceneTex, ImVec2((float)displayW_, (float)displayH_));
    const bool hovered = ImGui::IsItemHovered();

    if (selected) {
        const float aspect = float(displayW_) / float(displayH_);
        giz.beginFrame(imgPos.x, imgPos.y, (float)displayW_, (float)displayH_);
        using_ = giz.manipulate(camera.view(), camera.proj(aspect), *selected);
    }

    // A plain left-click on the image (not a gizmo drag) selects what's under
    // the cursor.
    if (view && hovered && !using_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 m = ImGui::GetMousePos();
        const float lx = m.x - imgPos.x, ly = m.y - imgPos.y;
        if (scene) {
            // Unified pick: dynamic entities + the whole loaded tile, nearest wins.
            lastWorldPick_ = pickWorldAt(lx, ly, *view, *scene);
            if (lastWorldPick_.isEntity()) {
                view->select(lastWorldPick_.guid);
                if (sceneSel) *sceneSel = WorldPick{};        // clear scene selection
            } else if (lastWorldPick_.isScene()) {
                view->select(0);                              // clear entity selection
                if (sceneSel) *sceneSel = lastWorldPick_;
            }
        } else if (terrain) {
            lastPick_ = pickAt(lx, ly, *view, *terrain);
            if (lastPick_.kind == PickResult::Kind::Entity)
                view->select(lastPick_.guid);
        }
    }

    ImGui::End();
    return using_;
}

} // namespace wf::editor
