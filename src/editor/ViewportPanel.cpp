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

bool ViewportPanel::draw(ImTextureID sceneTex, GizmoController& giz, Mat4* selected) {
    bool using_ = false;
    ImGui::Begin("Viewport");

    const ImVec2 imgPos = ImGui::GetCursorScreenPos();
    ImGui::Image(sceneTex, ImVec2((float)width_, (float)height_));

    if (selected) {
        const float aspect = float(width_) / float(height_);
        giz.beginFrame(imgPos.x, imgPos.y, (float)width_, (float)height_);
        using_ = giz.manipulate(camera.view(), camera.proj(aspect), *selected);
    }

    ImGui::End();
    return using_;
}

} // namespace wf::editor
