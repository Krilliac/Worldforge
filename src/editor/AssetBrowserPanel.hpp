#pragma once
// ---------------------------------------------------------------------------
// AssetBrowserPanel: browse the client's placeable models (M2 / WMO) from the
// AssetCatalog and pick one as the active placement asset. An M2/WMO tab pair
// chooses the kind, a text box filters the list by case-insensitive substring,
// and a scrollable list of selectables sets the current selection.
//
// As with the other editor panels, the UI state and the selection/filter logic
// are separated from the ImGui draw calls so the logic (filtered(), the
// selection getters) is unit-tested headless while draw() is the thin widget
// layer.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "asset_catalog.hpp"   // wf::ModelKind

namespace wf::editor {

class AssetBrowserPanel {
public:
    // Replace the browseable model lists (typically from AssetCatalog::listModels).
    void setModels(std::vector<std::string> m2s, std::vector<std::string> wmos);

    // Render the panel; window title "Assets".
    void draw();

    // Current tab's list filtered by `filter_` (case-insensitive substring;
    // empty filter returns the whole list).
    std::vector<std::string> filtered() const;

    bool hasSelection() const { return !selectedPath_.empty(); }
    const std::string& selectedPath() const { return selectedPath_; }
    ModelKind selectedKind() const { return kindTab_ == 0 ? ModelKind::M2 : ModelKind::Wmo; }
    void clearSelection() { selectedPath_.clear(); }

    // Select a path explicitly (the selected kind follows the active tab).
    void select(std::string path) { selectedPath_ = std::move(path); }

    // --- UI state (public for tests) ---
    int  kindTab_ = 0;             // 0 = M2, 1 = WMO
    char filter_[128] = {0};

private:
    std::vector<std::string> m2_, wmo_;
    std::string selectedPath_;
};

} // namespace wf::editor
