#include "editor/MapBrowserPanel.hpp"

#include "imgui.h"

namespace wf::editor {

void MapBrowserPanel::setMaps(std::vector<MapInfo> maps) {
    maps_ = std::move(maps);
    if (selectedMap_ >= static_cast<int>(maps_.size())) selectedMap_ = -1;
}

void MapBrowserPanel::setWdt(const Wdt& wdt) {
    tiles_ = wdt.tiles;
}

bool MapBrowserPanel::tilePresent(int x, int y) const {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return false;
    return tiles_[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)];
}

void MapBrowserPanel::selectTile(int x, int y) {
    if (!tilePresent(x, y)) return;             // no-op on an absent tile
    tileReq_ = true; tileX_ = x; tileY_ = y;
}

bool MapBrowserPanel::takeMapChanged(std::string& outMapName) {
    if (!mapChanged_) return false;
    outMapName = changedName_;
    mapChanged_ = false;
    return true;
}

bool MapBrowserPanel::takeTileRequest(int& x, int& y) {
    if (!tileReq_) return false;
    x = tileX_; y = tileY_;
    tileReq_ = false;
    return true;
}

void MapBrowserPanel::draw() {
    ImGui::Begin("Map Browser");

    // --- scrollable map list -----------------------------------------------
    ImGui::TextUnformatted("Maps");
    ImGui::BeginChild("maps", ImVec2(0, 140), true);
    for (int i = 0; i < static_cast<int>(maps_.size()); ++i) {
        const bool sel = (i == selectedMap_);
        if (ImGui::Selectable(maps_[i].name.c_str(), sel) && i != selectedMap_) {
            selectedMap_ = i;
            mapChanged_  = true;
            changedName_ = maps_[i].name;
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    // --- 64x64 tile grid ----------------------------------------------------
    ImGui::TextUnformatted("Tiles");
    const float canvas = 256.0f;
    const float cell   = canvas / 64.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("tilegrid", ImVec2(canvas, canvas));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 colAbsent   = IM_COL32(28, 30, 36, 255);
    const ImU32 colPresent  = IM_COL32(40, 150, 150, 255);
    const ImU32 colHovered  = IM_COL32(80, 220, 220, 255);
    const ImU32 colSelected = IM_COL32(240, 200, 90, 255);
    const ImU32 colGrid     = IM_COL32(60, 64, 72, 255);

    // Hovered cell under the mouse (if any).
    int hovX = -1, hovY = -1;
    if (ImGui::IsItemHovered()) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        hovX = static_cast<int>((mp.x - origin.x) / cell);
        hovY = static_cast<int>((mp.y - origin.y) / cell);
    }

    // Background canvas + per-cell fill.
    dl->AddRectFilled(origin, ImVec2(origin.x + canvas, origin.y + canvas), colAbsent);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (!tilePresent(x, y)) continue;
            const ImVec2 a(origin.x + x * cell, origin.y + y * cell);
            const ImVec2 b(a.x + cell, a.y + cell);
            ImU32 c = colPresent;
            if (x == hovX && y == hovY)            c = colHovered;
            if (tileReq_ && x == tileX_ && y == tileY_) c = colSelected;
            dl->AddRectFilled(a, b, c);
        }
    }
    // Grid outline (every 8 tiles + the border) so the 64x64 layout reads.
    for (int i = 0; i <= 64; i += 8) {
        dl->AddLine(ImVec2(origin.x + i * cell, origin.y),
                    ImVec2(origin.x + i * cell, origin.y + canvas), colGrid);
        dl->AddLine(ImVec2(origin.x, origin.y + i * cell),
                    ImVec2(origin.x + canvas, origin.y + i * cell), colGrid);
    }

    // Click inside the canvas -> map mouse to a cell and request it.
    if (ImGui::IsItemClicked() && hovX >= 0 && hovX < 64 && hovY >= 0 && hovY < 64) {
        selectTile(hovX, hovY);
    }

    ImGui::End();
}

} // namespace wf::editor
