#include "editor/ModelViewerPanel.hpp"

#include <algorithm>

namespace wf::editor {

void ModelViewerPanel::setModel(const M2Model& model, const M2Animation& anim, const Image& tex) {
    model_ = model; anim_ = anim; texture_ = tex; hasModel_ = true;
    sequence = 0; timeMs = 0.0f;
}

Mat4 ModelViewerPanel::viewMatrix() const {
    float ce = std::cos(elevation), se = std::sin(elevation);
    Vec3 eye = target + Vec3{ distance * ce * std::cos(azimuth),
                              distance * ce * std::sin(azimuth),
                              distance * se };
    return Mat4::lookAt(eye, target, Vec3{0, 0, 1});
}

void ModelViewerPanel::update(float dtMs) {
    if (!hasModel_ || !playing) return;
    if (sequence >= 0 && sequence < (int)anim_.sequences.size()) {
        uint32_t len = anim_.sequences[sequence].length;
        timeMs += dtMs;
        if (len > 0) timeMs = std::fmod(timeMs, (float)len);
    }
    render();
}

void ModelViewerPanel::render() {
    fb_.clear(Rgba{ 28, 30, 38, 255 });
    if (!hasModel_) { image_ = fb_.color; return; }

    TexMesh mesh = poseM2(model_, anim_, sequence, (uint32_t)timeMs);
    const float aspect = float(width_) / float(height_);
    Mat4 proj = Mat4::perspective(45.0, aspect, 0.05, 500.0);
    rasterTexMesh(fb_, mesh, proj * viewMatrix(), texture_, Vec3{0.4f, 0.3f, 0.85f});
    image_ = fb_.color;
}

void ModelViewerPanel::draw(ImTextureID sceneTex) {
    ImGui::Begin("Model Viewer");

    ImGui::Image(sceneTex, ImVec2((float)width_, (float)height_));

    ImGui::Separator();
    if (hasModel_ && !anim_.sequences.empty()) {
        int seqCount = (int)anim_.sequences.size();
        if (ImGui::SliderInt("Sequence", &sequence, 0, seqCount - 1)) timeMs = 0.0f;
        sequence = std::clamp(sequence, 0, seqCount - 1);
        uint32_t len = anim_.sequences[sequence].length;
        ImGui::Checkbox("Play", &playing);
        ImGui::SameLine();
        ImGui::SliderFloat("Time (ms)", &timeMs, 0.0f, len > 0 ? (float)len : 1.0f);
        ImGui::Text("Sequence id %u  length %u ms  bones %zu",
                    (unsigned)anim_.sequences[sequence].id, len, anim_.bones.size());
    } else {
        ImGui::TextUnformatted("No animation data");
    }
    ImGui::SliderFloat("Azimuth",   &azimuth,   -3.14159f, 3.14159f);
    ImGui::SliderFloat("Elevation", &elevation, -1.4f, 1.4f);
    ImGui::SliderFloat("Distance",  &distance,  1.0f, 40.0f);

    ImGui::End();
}

} // namespace wf::editor
