#include "editor/AssetBrowserPanel.hpp"

#include <algorithm>
#include <cctype>

#include "imgui.h"

namespace wf::editor {

namespace {
// Case-insensitive substring test (empty needle matches anything).
bool containsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    auto it = std::search(
        hay.begin(), hay.end(), needle.begin(), needle.end(),
        [&](char a, char b) { return lower(static_cast<unsigned char>(a)) ==
                                     lower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}
} // namespace

void AssetBrowserPanel::setModels(std::vector<std::string> m2s,
                                  std::vector<std::string> wmos) {
    m2_  = std::move(m2s);
    wmo_ = std::move(wmos);
}

std::vector<std::string> AssetBrowserPanel::filtered() const {
    const std::vector<std::string>& src = (kindTab_ == 0) ? m2_ : wmo_;
    const std::string needle(filter_);
    std::vector<std::string> out;
    out.reserve(src.size());
    for (const std::string& s : src)
        if (containsCI(s, needle)) out.push_back(s);
    return out;
}

void AssetBrowserPanel::draw() {
    ImGui::Begin("Assets");

    if (ImGui::BeginTabBar("AssetKind")) {
        if (ImGui::BeginTabItem("M2"))  { kindTab_ = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("WMO")) { kindTab_ = 1; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::InputText("Filter", filter_, sizeof filter_);

    const std::vector<std::string> list = filtered();
    ImGui::BeginChild("AssetList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3),
                      true);
    for (const std::string& path : list) {
        const bool sel = (path == selectedPath_);
        if (ImGui::Selectable(path.c_str(), sel))
            selectedPath_ = path;   // selectedKind() follows the active tab
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (hasSelection()) {
        ImGui::TextWrapped("Selected: %s", selectedPath_.c_str());
        if (ImGui::Button("Clear")) clearSelection();
    } else {
        ImGui::TextWrapped("No asset selected.");
    }

    ImGui::End();
}

} // namespace wf::editor
