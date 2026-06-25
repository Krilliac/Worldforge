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

void ViewportPanel::render(const Mesh& terrain, const DebugDraw& dd, const ShadeLight& light) {
    applyPendingResize();
    fb_.clear(Rgba{ 18, 20, 28, 255 });
    const float aspect = float(width_) / float(height_);
    const Mat4 mvp = camera.proj(aspect) * camera.view();
    rasterMesh(fb_, terrain, mvp, light);
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb_, const_cast<DebugDraw&>(dd), mvp, opt);
    scene_ = fb_.color;
}

void ViewportPanel::render(const TileScene& scene, const DebugDraw& dd, const ShadeLight& light) {
    applyPendingResize();
    fb_.clear(Rgba{ 18, 20, 28, 255 });
    const float aspect = float(width_) / float(height_);
    const Mat4 mvp = camera.proj(aspect) * camera.view();
    // Textured terrain + doodads + WMOs + liquid, lit by the tile's zone light;
    // renderLit() takes the sun direction and reads scene.light / liquidLight.
    scene.renderLit(fb_, mvp, light.dir);
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb_, const_cast<DebugDraw&>(dd), mvp, opt);
    scene_ = fb_.color;
}

void ViewportPanel::resize(int w, int h) {
    // Record the request only; render() reallocates on the next frame (see there).
    if (w < 1 || h < 1) return;
    pendingW_ = w;
    pendingH_ = h;
}

Ray ViewportPanel::rayAt(float localX, float localY) const {
    const float aspect = float(width_) / float(height_);
    return screenRay(camera.eye, camera.forward(), camera.right(), camera.up(),
                     camera.fovY, aspect, localX, localY, float(width_), float(height_));
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
    ImGui::Image(sceneTex, ImVec2((float)width_, (float)height_));
    const bool hovered = ImGui::IsItemHovered();

    if (selected) {
        const float aspect = float(width_) / float(height_);
        giz.beginFrame(imgPos.x, imgPos.y, (float)width_, (float)height_);
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
