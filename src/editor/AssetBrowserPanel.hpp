#pragma once
// ---------------------------------------------------------------------------
// AssetBrowserPanel: browse the client's placeable models (M2 / WMO) from the
// AssetCatalog and pick one as the active placement asset. An M2/WMO tab pair
// chooses the kind; a listfile folder tree on the left scopes the right pane
// to a directory; the right pane is either a clipped list or (given a
// ThumbnailCache) a thumbnail grid. The text filter is debounced (150 ms after
// the last keystroke), needs 3+ chars to kick in, matches a pre-lowercased
// shadow of the paths (no per-frame re-lowercasing), and has an opt-in regex
// mode where an invalid pattern matches nothing. Rows get a context menu (copy
// path / copy directory / pin favorite); favorites plus a 10-deep MRU of
// selected assets persist through saveBrowserState/loadBrowserState.
//
// As with the other editor panels, the UI state and the selection/filter logic
// are separated from the ImGui draw calls so the logic (filteredIndices(), the
// selection getters, the state (de)serialisers) is unit-tested headless while
// draw() is the thin widget layer. The debounce clock is injected
// (setClock) so tests advance time explicitly.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "asset_catalog.hpp"   // wf::ModelKind, AssetTree
#include "image.hpp"           // wf::Image (thumbnail resolver signature)
#include "imgui.h"             // ImTextureID

namespace wf::editor {

class ThumbnailCache;

// Persisted browser state: pinned favorites (pin order) + the most-recent-first
// MRU of selected assets, capped at kBrowserMruDepth. The MRU is what "place
// last-used doodad" reads later.
inline constexpr size_t kBrowserMruDepth = 10;

struct BrowserState {
    std::vector<std::string> favorites;
    std::vector<std::string> mru;
};

// Serialize/parse the state as a tiny ini-style UTF-8 text ([favorites] /
// [mru] sections, one path per line). Pure and lossless -- save(load(s)) == s
// -- so persistence is unit-tested as string equality. loadBrowserState is
// tolerant: unknown sections and blank lines are skipped, and the MRU is
// re-capped on load.
std::string  saveBrowserState(const BrowserState& s);
BrowserState loadBrowserState(const std::string& text);

// File forms (plain std::ofstream/ifstream, UTF-8 bytes as-is) for the host to
// persist next to the imgui ini. Return false on IO failure / missing file.
bool saveBrowserStateFile(const BrowserState& s, const std::string& path);
bool loadBrowserStateFile(BrowserState& out, const std::string& path);

class AssetBrowserPanel {
public:
    AssetBrowserPanel();

    // Replace the browseable model lists (typically from AssetCatalog::
    // listModels). Builds the folder trees + the lowercased filter shadows
    // once, here -- not per frame.
    void setModels(std::vector<std::string> m2s, std::vector<std::string> wmos);

    // Inject the now-milliseconds clock the filter debounce uses (tests pass a
    // fake; the default is steady_clock).
    void setClock(std::function<uint64_t()> nowMs);

    // Render the panel; window title "Assets". With a ThumbnailCache the right
    // pane offers the thumbnail grid (rendering thumbBudget_ pending previews
    // per frame); pass nullptr for the plain list.
    void draw(ThumbnailCache* thumbs = nullptr);

    // --- filtering (debounced, cached) --------------------------------------
    // Set the filter text programmatically (stamps the keystroke time).
    void setFilter(const std::string& text);
    // Indices into paths() passing the current filter/scope. Recomputed only
    // when something changed AND >= kDebounceMs has passed since the last
    // keystroke (structural changes -- tab, scope, toggles -- skip the wait).
    const std::vector<int>& filteredIndices();
    // The filtered paths, materialised (test/back-compat convenience).
    std::vector<std::string> filtered();
    // The active tab's full path list / folder tree.
    const std::vector<std::string>& paths() const { return kindTab_ == 0 ? m2_ : wmo_; }
    const AssetTree& tree() const { return kindTab_ == 0 ? m2Tree_ : wmoTree_; }
    // How many times the filter was recomputed (tests assert the debounce).
    int recomputeCount() const { return recomputeCount_; }

    // --- folder-tree scope ---------------------------------------------------
    // Scope the right pane to `dir` (a node of tree()); cleared by clearScope,
    // a tab switch, or setModels.
    void scopeTo(const AssetTreeNode& dir);
    void clearScope();
    bool scoped() const { return scopeNode_ != nullptr; }

    // --- selection / favorites / MRU -----------------------------------------
    bool hasSelection() const { return !selectedPath_.empty(); }
    const std::string& selectedPath() const { return selectedPath_; }
    ModelKind selectedKind() const { return kindTab_ == 0 ? ModelKind::M2 : ModelKind::Wmo; }
    void clearSelection() { selectedPath_.clear(); }

    // Select a path explicitly (the selected kind follows the active tab) and
    // record it in the MRU.
    void select(std::string path);

    void pinFavorite(const std::string& path);
    void unpinFavorite(const std::string& path);
    bool isFavorite(const std::string& path) const;
    const std::vector<std::string>& favorites() const { return state_.favorites; }
    const std::vector<std::string>& mru() const { return state_.mru; }

    BrowserState state() const { return state_; }
    void setState(BrowserState s);

    // --- clipboard (ImGui::SetClipboardText; headless-testable by hooking
    //     ImGui::GetPlatformIO().Platform_SetClipboardTextFn) ----------------
    void copyPath(const std::string& path) const;
    void copyDirectory(const std::string& path) const;   // path minus file name

    // --- UI state (public for tests) -----------------------------------------
    int  kindTab_ = 0;              // 0 = M2, 1 = WMO
    char filter_[128] = {0};
    bool regexMode_ = false;        // opt-in std::regex (icase); invalid -> no rows
    bool hideWmoGroups_ = true;     // WMO tab: hide _NNN.wmo group-file noise
    bool gridView_ = false;         // right pane: thumbnail grid vs list
    int  thumbBudget_ = 2;          // thumbnails rendered per frame in grid view

    // Host-supplied hook resolving a Ready thumbnail to an ImTextureID for
    // ImGui::ImageButton (e.g. registered with the SoftwareImGuiRenderer or
    // uploaded to GL). Unset -> the grid falls back to text buttons.
    std::function<ImTextureID(const std::string& path, const Image& thumb)> thumbTexture;

    static constexpr uint64_t kDebounceMs     = 150;
    static constexpr size_t   kMinFilterChars = 3;

private:
    void touchFilter();             // keystroke: dirty + debounce stamp
    void invalidate();              // structural change: recompute on next access
    void syncUiChanges();           // detect direct pokes at the public fields
    void recompute();
    void pushMru(const std::string& path);
    void drawTree(const AssetTreeNode& node);
    void drawRowContextMenu(const std::string& path);
    void drawList();
    void drawGrid(ThumbnailCache& thumbs);

    std::vector<std::string> m2_, wmo_;
    std::vector<std::string> m2Lower_, wmoLower_;   // pre-lowercased shadows
    AssetTree m2Tree_, wmoTree_;

    std::string  selectedPath_;
    BrowserState state_;

    std::function<uint64_t()> nowMs_;
    std::vector<int> filteredIdx_;
    bool     dirty_ = true;
    bool     debounce_ = false;     // dirty via keystroke -> honour kDebounceMs
    uint64_t lastKeystrokeMs_ = 0;
    int      recomputeCount_ = 0;

    // Shadows of the public fields, to catch direct pokes (tests, host code).
    std::string lastFilterText_;
    int  lastKindTab_ = 0;
    bool lastRegexMode_ = false;
    bool lastHideWmoGroups_ = true;

    const AssetTreeNode* scopeNode_ = nullptr;   // points into the active tree
    std::vector<int>     scopeIdx_;              // sorted subtreePaths(*scopeNode_)
};

} // namespace wf::editor
