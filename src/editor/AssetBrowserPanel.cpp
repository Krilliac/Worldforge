#include "editor/AssetBrowserPanel.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

#include "editor/ThumbnailCache.hpp"

namespace wf::editor {

namespace {
std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}
} // namespace

// --- browser state persistence (pure text form + thin file wrappers) --------

std::string saveBrowserState(const BrowserState& s) {
    std::string out = "[favorites]\n";
    for (const std::string& f : s.favorites) { out += f; out += '\n'; }
    out += "[mru]\n";
    for (const std::string& m : s.mru) { out += m; out += '\n'; }
    return out;
}

BrowserState loadBrowserState(const std::string& text) {
    BrowserState s;
    std::vector<std::string>* section = nullptr;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line == "[favorites]") { section = &s.favorites; continue; }
        if (line == "[mru]")       { section = &s.mru;       continue; }
        if (line.front() == '[')   { section = nullptr;      continue; }  // unknown
        if (section) section->push_back(line);
    }
    if (s.mru.size() > kBrowserMruDepth) s.mru.resize(kBrowserMruDepth);
    return s;
}

bool saveBrowserStateFile(const BrowserState& s, const std::string& path) {
    std::ofstream out(path, std::ios::binary);   // UTF-8 bytes as-is
    if (!out) return false;
    const std::string text = saveBrowserState(s);
    out.write(text.data(), (std::streamsize)text.size());
    return out.good();
}

bool loadBrowserStateFile(BrowserState& out, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = loadBrowserState(text);
    return true;
}

// --- panel -------------------------------------------------------------------

AssetBrowserPanel::AssetBrowserPanel() {
    nowMs_ = [] {
        using namespace std::chrono;
        return (uint64_t)duration_cast<milliseconds>(
                   steady_clock::now().time_since_epoch()).count();
    };
}

void AssetBrowserPanel::setModels(std::vector<std::string> m2s,
                                  std::vector<std::string> wmos) {
    m2_  = std::move(m2s);
    wmo_ = std::move(wmos);
    // Build the filter shadows + folder trees ONCE, here -- filtering rescans
    // pre-lowercased strings and the trees are stable until the next setModels.
    m2Lower_.clear();  m2Lower_.reserve(m2_.size());
    for (const std::string& p : m2_)  m2Lower_.push_back(lowerAscii(p));
    wmoLower_.clear(); wmoLower_.reserve(wmo_.size());
    for (const std::string& p : wmo_) wmoLower_.push_back(lowerAscii(p));
    m2Tree_  = buildAssetTree(m2_);
    wmoTree_ = buildAssetTree(wmo_);
    scopeNode_ = nullptr;
    scopeIdx_.clear();
    invalidate();
}

void AssetBrowserPanel::setClock(std::function<uint64_t()> nowMs) {
    if (nowMs) nowMs_ = std::move(nowMs);
}

void AssetBrowserPanel::setFilter(const std::string& text) {
    std::snprintf(filter_, sizeof filter_, "%s", text.c_str());
    lastFilterText_ = filter_;
    touchFilter();
}

void AssetBrowserPanel::touchFilter() {
    dirty_ = true;
    debounce_ = true;
    lastKeystrokeMs_ = nowMs_();
}

void AssetBrowserPanel::invalidate() {
    dirty_ = true;
    debounce_ = false;   // structural change: no keystroke wait
}

void AssetBrowserPanel::syncUiChanges() {
    if (kindTab_ != lastKindTab_) {
        lastKindTab_ = kindTab_;
        scopeNode_ = nullptr;        // the scope pointed into the other tree
        scopeIdx_.clear();
        invalidate();
    }
    if (regexMode_ != lastRegexMode_ || hideWmoGroups_ != lastHideWmoGroups_) {
        lastRegexMode_ = regexMode_;
        lastHideWmoGroups_ = hideWmoGroups_;
        invalidate();
    }
    if (lastFilterText_ != filter_) {   // buffer poked directly (host/tests)
        lastFilterText_ = filter_;
        touchFilter();
    }
}

const std::vector<int>& AssetBrowserPanel::filteredIndices() {
    syncUiChanges();
    if (dirty_ && (!debounce_ || nowMs_() - lastKeystrokeMs_ >= kDebounceMs))
        recompute();
    return filteredIdx_;
}

void AssetBrowserPanel::recompute() {
    dirty_ = false;
    debounce_ = false;
    ++recomputeCount_;
    filteredIdx_.clear();

    const std::vector<std::string>& list  = paths();
    const std::vector<std::string>& lower = (kindTab_ == 0) ? m2Lower_ : wmoLower_;
    const std::string needle = lowerAscii(filter_);
    const bool useFilter = needle.size() >= kMinFilterChars;   // below: show all
    const bool dropGroups = (kindTab_ == 1) && hideWmoGroups_;

    std::regex re;
    if (regexMode_ && useFilter) {
        try {
            re = std::regex(filter_, std::regex::icase);
        } catch (const std::regex_error&) {
            return;   // invalid pattern matches NOTHING (and never throws to the UI)
        }
    }

    auto pass = [&](int i) {
        if (dropGroups && isWmoGroupFile(list[(size_t)i])) return false;
        if (!useFilter) return true;
        if (regexMode_) return std::regex_search(list[(size_t)i], re);
        return lower[(size_t)i].find(needle) != std::string::npos;
    };

    if (scopeNode_) {
        for (int i : scopeIdx_)
            if (pass(i)) filteredIdx_.push_back(i);
    } else {
        for (int i = 0; i < (int)list.size(); ++i)
            if (pass(i)) filteredIdx_.push_back(i);
    }
}

std::vector<std::string> AssetBrowserPanel::filtered() {
    const std::vector<int>& idx = filteredIndices();
    const std::vector<std::string>& list = paths();
    std::vector<std::string> out;
    out.reserve(idx.size());
    for (int i : idx) out.push_back(list[(size_t)i]);
    return out;
}

void AssetBrowserPanel::scopeTo(const AssetTreeNode& dir) {
    syncUiChanges();   // absorb a pending kind-tab switch so it can't clobber this scope
    scopeNode_ = &dir;
    scopeIdx_ = subtreePaths(dir);
    invalidate();
}

void AssetBrowserPanel::clearScope() {
    if (!scopeNode_) return;
    scopeNode_ = nullptr;
    scopeIdx_.clear();
    invalidate();
}

void AssetBrowserPanel::select(std::string path) {
    if (!path.empty()) pushMru(path);
    selectedPath_ = std::move(path);
}

void AssetBrowserPanel::pushMru(const std::string& path) {
    auto& mru = state_.mru;
    mru.erase(std::remove(mru.begin(), mru.end(), path), mru.end());
    mru.insert(mru.begin(), path);
    if (mru.size() > kBrowserMruDepth) mru.resize(kBrowserMruDepth);
}

void AssetBrowserPanel::pinFavorite(const std::string& path) {
    if (!isFavorite(path)) state_.favorites.push_back(path);
}

void AssetBrowserPanel::unpinFavorite(const std::string& path) {
    auto& fav = state_.favorites;
    fav.erase(std::remove(fav.begin(), fav.end(), path), fav.end());
}

bool AssetBrowserPanel::isFavorite(const std::string& path) const {
    const auto& fav = state_.favorites;
    return std::find(fav.begin(), fav.end(), path) != fav.end();
}

void AssetBrowserPanel::setState(BrowserState s) {
    state_ = std::move(s);
    if (state_.mru.size() > kBrowserMruDepth) state_.mru.resize(kBrowserMruDepth);
}

void AssetBrowserPanel::copyPath(const std::string& path) const {
    ImGui::SetClipboardText(path.c_str());
}

void AssetBrowserPanel::copyDirectory(const std::string& path) const {
    size_t s = path.find_last_of("\\/");
    ImGui::SetClipboardText(s == std::string::npos ? "" : path.substr(0, s).c_str());
}

// --- ImGui layer ---------------------------------------------------------

void AssetBrowserPanel::drawRowContextMenu(const std::string& path) {
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Copy path"))      copyPath(path);
        if (ImGui::MenuItem("Copy directory")) copyDirectory(path);
        if (isFavorite(path)) {
            if (ImGui::MenuItem("Unpin favorite")) unpinFavorite(path);
        } else {
            if (ImGui::MenuItem("Pin favorite")) pinFavorite(path);
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::drawTree(const AssetTreeNode& node) {
    for (const auto& [key, child] : node.dirs) {
        ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                ImGuiTreeNodeFlags_SpanAvailWidth;
        if (&child == scopeNode_) fl |= ImGuiTreeNodeFlags_Selected;
        if (child.dirs.empty())   fl |= ImGuiTreeNodeFlags_Leaf;
        const bool open = ImGui::TreeNodeEx(child.name.c_str(), fl);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            scopeTo(child);   // selecting a dir scopes the right pane
        if (open) {
            drawTree(child);
            ImGui::TreePop();
        }
    }
}

void AssetBrowserPanel::drawList() {
    const std::vector<int>& idx = filteredIndices();
    const std::vector<std::string>& list = paths();
    // Clipped: 40k paths cost ~the visible rows, not 40k Selectables.
    ImGuiListClipper clipper;
    clipper.Begin((int)idx.size());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::string& path = list[(size_t)idx[(size_t)row]];
            if (ImGui::Selectable(path.c_str(), path == selectedPath_))
                select(path);
            drawRowContextMenu(path);
        }
    }
}

void AssetBrowserPanel::drawGrid(ThumbnailCache& thumbs) {
    const std::vector<int>& idx = filteredIndices();
    const std::vector<std::string>& list = paths();
    const ImVec2 cell((float)thumbs.dim(), (float)thumbs.dim());
    const float pitch = cell.x + ImGui::GetStyle().ItemSpacing.x;
    const int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / pitch));
    const int rows = ((int)idx.size() + cols - 1) / cols;

    ImGuiListClipper clipper;   // clip whole grid rows
    clipper.Begin(rows, cell.y + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int i = r * cols + c;
                if (i >= (int)idx.size()) break;
                const std::string& path = list[(size_t)idx[(size_t)i]];
                if (c > 0) ImGui::SameLine();
                ImGui::PushID(path.c_str());
                const ThumbResult t = thumbs.get(path);
                bool clicked = false;
                if (t.status == ThumbStatus::Ready && t.image && thumbTexture)
                    clicked = ImGui::ImageButton("thumb", thumbTexture(path, *t.image), cell);
                else
                    clicked = ImGui::Button(t.status == ThumbStatus::Failed ? "!" : "...", cell);
                if (clicked) select(path);
                drawRowContextMenu(path);
                ImGui::PopID();
            }
        }
    }
}

void AssetBrowserPanel::draw(ThumbnailCache* thumbs) {
    ImGui::Begin("Assets");

    if (ImGui::BeginTabBar("AssetKind")) {
        if (ImGui::BeginTabItem("M2"))  { kindTab_ = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("WMO")) { kindTab_ = 1; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    if (ImGui::InputText("Filter", filter_, sizeof filter_)) {
        lastFilterText_ = filter_;
        touchFilter();               // debounced; recompute >=150ms after typing
    }
    ImGui::Checkbox("Regex", &regexMode_);
    if (kindTab_ == 1) { ImGui::SameLine(); ImGui::Checkbox("Hide group files", &hideWmoGroups_); }
    if (thumbs)        { ImGui::SameLine(); ImGui::Checkbox("Grid", &gridView_); }

    const float footer = ImGui::GetFrameHeightWithSpacing() * 3;

    // Left: favorites + recent + the listfile folder tree (scopes the right pane).
    ImGui::BeginChild("AssetTreePane", ImVec2(230, -footer), true);
    if (!state_.favorites.empty() && ImGui::CollapsingHeader("Favorites")) {
        for (const std::string& f : state_.favorites) {
            ImGui::PushID(f.c_str());
            if (ImGui::Selectable(f.c_str(), f == selectedPath_)) select(f);
            drawRowContextMenu(f);
            ImGui::PopID();
        }
    }
    if (!state_.mru.empty() && ImGui::CollapsingHeader("Recent")) {
        for (const std::string& m : state_.mru) {
            ImGui::PushID(m.c_str());
            if (ImGui::Selectable(m.c_str(), m == selectedPath_)) select(m);
            drawRowContextMenu(m);
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    if (ImGui::Selectable("<all folders>", !scoped())) clearScope();
    drawTree(tree().root);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: the clipped list, or the thumbnail grid when a cache is supplied.
    ImGui::BeginChild("AssetListPane", ImVec2(0, -footer), true);
    if (thumbs && gridView_) {
        thumbs->generateBudget(thumbBudget_);   // per-frame render budget
        drawGrid(*thumbs);
    } else {
        drawList();
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
