#include "test.hpp"
#include "imgui.h"

#include "editor/AssetBrowserPanel.hpp"
#include "asset_catalog.hpp"

#include <string>
#include <vector>

using namespace wf;
using namespace wf::editor;

void test_asset_browser() {
    std::printf("[editor.asset_browser]\n");

    AssetBrowserPanel panel;
    std::vector<std::string> m2s = {
        "World\\Creature\\Murloc\\Murloc.m2",
        "World\\Creature\\Kobold\\Kobold.m2",
        "Item\\ObjectComponents\\Weapon\\Sword_01.m2",
    };
    std::vector<std::string> wmos = {
        "World\\wmo\\Azeroth\\Buildings\\Blacksmith\\Blacksmith.wmo",
        "World\\wmo\\Dungeon\\Cave\\Cave.wmo",
    };
    panel.setModels(m2s, wmos);

    // --- default tab is M2 -> filtered() is the whole M2 list -----------------
    CHECK(panel.kindTab_ == 0);
    CHECK(panel.selectedKind() == ModelKind::M2);
    {
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == m2s.size());
        CHECK(f == m2s);
    }

    // --- a filter substring narrows the list (and is case-insensitive) --------
    std::snprintf(panel.filter_, sizeof panel.filter_, "kobold");
    {
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == 1);
        CHECK(f.front() == "World\\Creature\\Kobold\\Kobold.m2");
    }
    // Mixed case needle still matches.
    std::snprintf(panel.filter_, sizeof panel.filter_, "MuRLoC");
    {
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == 1);
        CHECK(f.front() == "World\\Creature\\Murloc\\Murloc.m2");
    }
    // A needle that matches nothing yields an empty list.
    std::snprintf(panel.filter_, sizeof panel.filter_, "zzznope");
    CHECK(panel.filtered().empty());

    // Empty filter restores the whole list.
    panel.filter_[0] = '\0';
    CHECK(panel.filtered().size() == m2s.size());

    // --- flipping to the WMO tab draws from the WMO list ----------------------
    panel.kindTab_ = 1;
    CHECK(panel.selectedKind() == ModelKind::Wmo);
    {
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == wmos.size());
        CHECK(f == wmos);
    }
    std::snprintf(panel.filter_, sizeof panel.filter_, "cave");
    {
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == 1);
        CHECK(f.front() == "World\\wmo\\Dungeon\\Cave\\Cave.wmo");
    }
    panel.filter_[0] = '\0';

    // --- selection getters / clearSelection -----------------------------------
    CHECK(!panel.hasSelection());
    panel.select("World\\wmo\\Dungeon\\Cave\\Cave.wmo");
    CHECK(panel.hasSelection());
    CHECK(panel.selectedPath() == "World\\wmo\\Dungeon\\Cave\\Cave.wmo");
    CHECK(panel.selectedKind() == ModelKind::Wmo);   // follows the active tab

    // Selecting on the M2 tab reports the M2 kind.
    panel.kindTab_ = 0;
    panel.select("World\\Creature\\Murloc\\Murloc.m2");
    CHECK(panel.selectedKind() == ModelKind::M2);
    CHECK(panel.selectedPath() == "World\\Creature\\Murloc\\Murloc.m2");

    panel.clearSelection();
    CHECK(!panel.hasSelection());
    CHECK(panel.selectedPath().empty());

    // --- one headless draw() pass exercises the ImGui widget layer ------------
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_Always);
    panel.draw();
    ImGui::Render();
    CHECK(ImGui::GetDrawData() != nullptr);
}
