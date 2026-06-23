#include "editor/ViewportPanel.hpp"

namespace wf::editor {

void ViewportPanel::render(const Mesh& terrain, const DebugDraw& dd) {
    fb_.clear(Rgba{ 18, 20, 28, 255 });
    const float aspect = float(width_) / float(height_);
    const Mat4 mvp = camera.proj(aspect) * camera.view();
    rasterMesh(fb_, terrain, mvp, Vec3{ 0.5f, 0.4f, 0.8f });
    DebugDrawOptions opt; opt.depthTest = true;
    rasterDebug(fb_, const_cast<DebugDraw&>(dd), mvp, opt);
    scene_ = fb_.color;
}

PickResult ViewportPanel::pickAt(float localX, float localY,
                                 const WorldView& view, const Mesh& terrain) const {
    const float aspect = float(width_) / float(height_);
    Ray r = screenRay(camera.eye, camera.forward(), camera.right(), camera.up(),
                      camera.fovY, aspect, localX, localY, float(width_), float(height_));
    return pick(r, view, terrain);
}

bool ViewportPanel::draw(ImTextureID sceneTex, GizmoController& giz, Mat4* selected,
                         WorldView* view, const Mesh* terrain) {
    bool using_ = false;
    ImGui::Begin("Viewport");

    const ImVec2 imgPos = ImGui::GetCursorScreenPos();
    ImGui::Image(sceneTex, ImVec2((float)width_, (float)height_));
    const bool hovered = ImGui::IsItemHovered();

    if (selected) {
        const float aspect = float(width_) / float(height_);
        giz.beginFrame(imgPos.x, imgPos.y, (float)width_, (float)height_);
        using_ = giz.manipulate(camera.view(), camera.proj(aspect), *selected);
    }

    // A plain left-click on the image (not a gizmo drag) selects what's under
    // the cursor. Entities update the shared WorldView selection so the
    // inspector + gizmo follow; terrain hits are reported via lastPick().
    if (view && terrain && hovered && !using_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 m = ImGui::GetMousePos();
        lastPick_ = pickAt(m.x - imgPos.x, m.y - imgPos.y, *view, *terrain);
        if (lastPick_.kind == PickResult::Kind::Entity)
            view->select(lastPick_.guid);
    }

    ImGui::End();
    return using_;
}

} // namespace wf::editor
