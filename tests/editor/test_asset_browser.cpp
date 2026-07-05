#include "test.hpp"
#include "imgui.h"

#include "editor/AssetBrowserPanel.hpp"
#include "editor/ThumbnailCache.hpp"
#include "asset_catalog.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace wf;
using namespace wf::editor;

namespace {

std::string g_clipboard;
void captureClipboard(ImGuiContext*, const char* text) { g_clipboard = text ? text : ""; }

// A synthetic one-triangle mesh + a white 2x2 texture -- the ThumbnailCache
// loader for tests (no MPQ, no real models).
ThumbSource makeTriSource() {
    ThumbSource s;
    s.mesh.vertices = {
        { {0, 0, 0}, {0, 0, 1}, {0, 0} },
        { {2, 0, 0}, {0, 0, 1}, {1, 0} },
        { {0, 2, 1}, {0, 0, 1}, {0, 1} },
    };
    s.mesh.indices = { 0, 1, 2 };
    s.image = Image(2, 2);
    for (Rgba& p : s.image.pixels) p = Rgba{ 255, 255, 255, 255 };
    s.ok = true;
    return s;
}

bool anyNonBackdrop(const Image& img) {
    for (const Rgba& p : img.pixels)
        if (!(p.r == 28 && p.g == 30 && p.b == 38)) return true;
    return false;
}

} // namespace

void test_asset_browser() {
    std::printf("[editor.asset_browser]\n");

    AssetBrowserPanel panel;
    uint64_t now = 1000;
    panel.setClock([&now] { return now; });

    std::vector<std::string> m2s = {
        "World\\Creature\\Murloc\\Murloc.m2",
        "World\\Creature\\Kobold\\Kobold.m2",
        "Item\\ObjectComponents\\Weapon\\Sword_01.m2",
    };
    std::vector<std::string> wmos = {
        "World\\wmo\\Azeroth\\Buildings\\Blacksmith\\Blacksmith.wmo",
        "World\\wmo\\Dungeon\\Cave\\Cave.wmo",
        "World\\wmo\\Dungeon\\Cave\\Cave_000.wmo",   // group-file noise
    };
    panel.setModels(m2s, wmos);

    // --- default tab is M2 -> filtered() is the whole M2 list -----------------
    CHECK(panel.kindTab_ == 0);
    CHECK(panel.selectedKind() == ModelKind::M2);
    CHECK(panel.filtered() == m2s);

    // --- debounce: recompute only >=150ms after the last keystroke ------------
    {
        const int c0 = panel.recomputeCount();
        panel.setFilter("kob");
        panel.setFilter("kobold");                 // two rapid keystrokes
        now += 100;                                // 100ms < 150ms
        CHECK(panel.filteredIndices().size() == m2s.size());   // stale, no recompute
        CHECK(panel.recomputeCount() == c0);
        now += 100;                                // 200ms since the keystroke
        std::vector<std::string> f = panel.filtered();
        CHECK(panel.recomputeCount() == c0 + 1);   // recomputed exactly once
        CHECK(f.size() == 1);
        CHECK(f.front() == "World\\Creature\\Kobold\\Kobold.m2");
        panel.filtered();                          // cached: no further recompute
        CHECK(panel.recomputeCount() == c0 + 1);
    }

    // --- 3-character minimum: a 2-char filter shows everything ----------------
    panel.setFilter("ko");
    now += 1000;
    CHECK(panel.filtered().size() == m2s.size());

    // --- substring match is case-insensitive (pre-lowercased shadow) ----------
    panel.setFilter("MuRLoC");
    now += 1000;
    {
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == 1);
        CHECK(f.front() == "World\\Creature\\Murloc\\Murloc.m2");
    }

    // --- regex mode: icase; an INVALID pattern matches NOTHING (no throw) -----
    panel.regexMode_ = true;
    panel.setFilter("((((bad");                    // invalid regex
    now += 1000;
    CHECK(panel.filtered().empty());
    panel.setFilter("kob.ld.*\\.m2");              // valid, icase
    now += 1000;
    CHECK(panel.filtered().size() == 1);
    panel.regexMode_ = false;
    panel.setFilter("");
    now += 1000;
    CHECK(panel.filtered() == m2s);

    // --- WMO tab: group files hidden by default; toggle restores them ---------
    panel.kindTab_ = 1;
    CHECK(panel.selectedKind() == ModelKind::Wmo);
    {
        std::vector<std::string> f = panel.filtered();   // hideWmoGroups_ default on
        CHECK(f.size() == 2);
        for (const std::string& p : f) CHECK(!isWmoGroupFile(p));
        panel.hideWmoGroups_ = false;
        CHECK(panel.filtered().size() == wmos.size());
        panel.hideWmoGroups_ = true;
        CHECK(panel.filtered().size() == 2);
    }

    // --- folder tree scope: selecting a dir scopes the pane -------------------
    panel.kindTab_ = 0;
    {
        const AssetTree& t = panel.tree();
        CHECK(t.root.dirs.count("world") == 1 && t.root.dirs.count("item") == 1);
        const AssetTreeNode& creature = t.root.dirs.at("world").dirs.at("creature");
        panel.scopeTo(creature);
        CHECK(panel.scoped());
        std::vector<std::string> f = panel.filtered();
        CHECK(f.size() == 2);                      // the two Creature models only
        panel.setFilter("kobold");
        now += 1000;
        CHECK(panel.filtered().size() == 1);       // scope + filter compose
        panel.setFilter("");
        panel.clearScope();
        now += 1000;
        CHECK(!panel.scoped());
        CHECK(panel.filtered() == m2s);
    }

    // --- selection + MRU (10-deep, most-recent-first, deduplicated) -----------
    CHECK(!panel.hasSelection());
    panel.select("World\\Creature\\Murloc\\Murloc.m2");
    panel.select("World\\Creature\\Kobold\\Kobold.m2");
    CHECK(panel.hasSelection());
    CHECK(panel.selectedPath() == "World\\Creature\\Kobold\\Kobold.m2");
    CHECK(panel.mru().size() == 2);
    CHECK(panel.mru()[0] == "World\\Creature\\Kobold\\Kobold.m2");   // newest first
    panel.select("World\\Creature\\Murloc\\Murloc.m2");             // re-select
    CHECK(panel.mru().size() == 2);                                  // deduplicated
    CHECK(panel.mru()[0] == "World\\Creature\\Murloc\\Murloc.m2");
    for (int i = 0; i < 12; ++i)
        panel.select("mru_filler_" + std::to_string(i) + ".m2");
    CHECK(panel.mru().size() == kBrowserMruDepth);                   // capped at 10
    CHECK(panel.mru()[0] == "mru_filler_11.m2");
    panel.clearSelection();
    CHECK(!panel.hasSelection());

    // --- favorites pin/unpin ---------------------------------------------------
    panel.pinFavorite("World\\wmo\\Dungeon\\Cave\\Cave.wmo");
    panel.pinFavorite("World\\wmo\\Dungeon\\Cave\\Cave.wmo");        // no duplicate
    panel.pinFavorite("Item\\ObjectComponents\\Weapon\\Sword_01.m2");
    CHECK(panel.favorites().size() == 2);
    CHECK(panel.isFavorite("World\\wmo\\Dungeon\\Cave\\Cave.wmo"));
    panel.unpinFavorite("World\\wmo\\Dungeon\\Cave\\Cave.wmo");
    CHECK(panel.favorites().size() == 1);
    CHECK(!panel.isFavorite("World\\wmo\\Dungeon\\Cave\\Cave.wmo"));

    // --- favorites/MRU round-trip through saveBrowserState/loadBrowserState ---
    {
        BrowserState s;
        s.favorites = { "World\\wmo\\Dungeon\\Cave\\Cave.wmo",
                        "Item\\ObjectComponents\\Weapon\\Sword_01.m2" };
        s.mru = { "World\\Creature\\Kobold\\Kobold.m2",
                  "World\\Creature\\Murloc\\Murloc.m2" };
        const std::string text = saveBrowserState(s);
        BrowserState back = loadBrowserState(text);
        CHECK(back.favorites == s.favorites);
        CHECK(back.mru == s.mru);
        CHECK(saveBrowserState(back) == text);                       // lossless

        // Empty state round-trips too.
        CHECK(saveBrowserState(loadBrowserState(saveBrowserState(BrowserState{}))) ==
              saveBrowserState(BrowserState{}));

        // File form (plain UTF-8 next to the imgui ini).
        const char* stateFile = "wforge_browser_state_test.ini";
        CHECK(saveBrowserStateFile(s, stateFile));
        BrowserState fromFile;
        CHECK(loadBrowserStateFile(fromFile, stateFile));
        CHECK(saveBrowserState(fromFile) == text);
        std::remove(stateFile);
        CHECK(!loadBrowserStateFile(fromFile, "wforge_no_such_state.ini"));

        panel.setState(s);
        CHECK(panel.favorites() == s.favorites && panel.mru() == s.mru);
    }

    // --- clipboard: the hook receives the exact path on copy-path -------------
    {
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        auto oldFn = pio.Platform_SetClipboardTextFn;
        pio.Platform_SetClipboardTextFn = captureClipboard;
        panel.copyPath("World\\Creature\\Kobold\\Kobold.m2");
        CHECK(g_clipboard == "World\\Creature\\Kobold\\Kobold.m2");
        panel.copyDirectory("World\\Creature\\Kobold\\Kobold.m2");
        CHECK(g_clipboard == "World\\Creature\\Kobold");
        panel.copyDirectory("loosefile.m2");                         // no directory
        CHECK(g_clipboard.empty());
        pio.Platform_SetClipboardTextFn = oldFn;
    }

    // --- empty listfile: everything stays empty and calm ----------------------
    {
        AssetBrowserPanel bare;
        CHECK(bare.filtered().empty());
        CHECK(subtreePaths(bare.tree().root).empty());
    }

    // --- ThumbnailCache: pending -> budgeted render -> ready + disk cache -----
    namespace fs = std::filesystem;
    const char* cacheDir = "wforge_thumb_cache_test";
    fs::remove_all(cacheDir);
    auto sizeFn = [](const std::string&) -> uint64_t { return 123; };
    {
        int loads = 0;
        ThumbnailCache cache(cacheDir, sizeFn,
                             [&loads](const std::string&) { ++loads; return makeTriSource(); });

        CHECK(cache.get("tri.m2").status == ThumbStatus::Pending);   // queued, not rendered
        CHECK(loads == 0);
        CHECK(cache.generateBudget(1) == 1);                         // per-frame budget
        ThumbResult r = cache.get("tri.m2");
        CHECK(r.status == ThumbStatus::Ready);
        CHECK(r.image && r.image->width == 96 && r.image->height == 96);
        CHECK(anyNonBackdrop(*r.image));                             // triangle drew pixels
        CHECK(fs::exists(cache.diskPathFor("tri.m2")));              // PNG persisted
        CHECK(loads == 1);

        // The budget really bounds work: two pending, one call renders one.
        cache.get("a.m2");
        cache.get("b.m2");
        CHECK(cache.pendingCount() == 2);
        CHECK(cache.generateBudget(1) == 1);
        CHECK(cache.pendingCount() == 1);
    }
    {
        // A second cache over the same dir: disk hit, loader NEVER invoked.
        int loads = 0;
        ThumbnailCache cache(cacheDir, sizeFn,
                             [&loads](const std::string&) { ++loads; return makeTriSource(); });
        ThumbResult r = cache.get("tri.m2");
        CHECK(r.status == ThumbStatus::Ready);
        CHECK(r.image && r.image->width == 96 && anyNonBackdrop(*r.image));
        CHECK(loads == 0);

        // A size change alters the key -> the old PNG no longer matches.
        ThumbnailCache resized(cacheDir, [](const std::string&) -> uint64_t { return 999; },
                               [&loads](const std::string&) { ++loads; return makeTriSource(); });
        CHECK(resized.get("tri.m2").status == ThumbStatus::Pending);
    }
    {
        // A failing loader is cached as Failed and invoked exactly once.
        int fails = 0;
        ThumbnailCache cache(cacheDir, sizeFn,
                             [&fails](const std::string&) { ++fails; return ThumbSource{}; });
        CHECK(cache.get("bad.m2").status == ThumbStatus::Pending);
        CHECK(cache.generateBudget(4) == 1);
        CHECK(cache.get("bad.m2").status == ThumbStatus::Failed);
        cache.generateBudget(4);                                     // nothing to retry
        CHECK(cache.get("bad.m2").status == ThumbStatus::Failed);
        CHECK(fails == 1);

        // An image-only source (the BLP path) scales instead of rasterising.
        ThumbnailCache imgCache(cacheDir, sizeFn, [](const std::string&) {
            ThumbSource s;
            s.image = Image(4, 8);
            for (Rgba& p : s.image.pixels) p = Rgba{ 200, 40, 40, 255 };
            s.ok = true;
            return s;
        });
        imgCache.get("red.blp");
        imgCache.generateBudget(1);
        ThumbResult r = imgCache.get("red.blp");
        CHECK(r.status == ThumbStatus::Ready);
        CHECK(r.image && r.image->width == 96 && r.image->height == 96);
        bool red = false;
        for (const Rgba& p : r.image->pixels)
            if (p.r > 150 && p.g < 100) { red = true; break; }
        CHECK(red);
    }
    fs::remove_all(cacheDir);

    // --- one headless draw() pass exercises the ImGui widget layer ------------
    panel.setFilter("");
    now += 1000;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Always);
    panel.draw();
    ImGui::Render();
    CHECK(ImGui::GetDrawData() != nullptr);

    // ...and one grid-view pass with a live ThumbnailCache + texture hook.
    {
        ThumbnailCache cache(cacheDir, sizeFn,
                             [](const std::string&) { return makeTriSource(); });
        panel.gridView_ = true;
        panel.thumbTexture = [](const std::string&, const Image&) {
            return (ImTextureID)3;   // dummy id, like the other headless tests
        };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Always);
        panel.draw(&cache);
        ImGui::Render();
        CHECK(ImGui::GetDrawData() != nullptr);
        CHECK(cache.pendingCount() > 0);           // grid queued visible thumbnails
        panel.gridView_ = false;
    }
    fs::remove_all(cacheDir);
}
