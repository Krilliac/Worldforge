#include "test.hpp"
#include "imgui.h"

#include "editor/MapBrowserPanel.hpp"
#include "wow_files.hpp"
#include "asset_catalog.hpp"

#include <string>
#include <vector>

using namespace wf;
using namespace wf::editor;

void test_map_browser() {
    std::printf("[editor.map_browser]\n");

    MapBrowserPanel panel;

    // --- map catalog + edge-triggered map change ----------------------------
    std::vector<MapInfo> maps = {
        { "Azeroth",  "World\\Maps\\Azeroth\\Azeroth.wdt" },
        { "Kalimdor", "World\\Maps\\Kalimdor\\Kalimdor.wdt" },
    };
    panel.setMaps(maps);
    CHECK(panel.selectedMap() == -1);

    // Picking a map (set the public UI state, then flag it as the host's draw
    // would) edge-triggers once.
    panel.selectedMap_ = 1;
    std::string name;
    // No change flagged yet -> takeMapChanged is false.
    CHECK(!panel.takeMapChanged(name));

    // --- tile presence from a WDT -------------------------------------------
    Wdt wdt;
    wdt.tiles[32 * 64 + 30] = true;   // present tile (x=30, y=32)
    wdt.tiles[10 * 64 + 5]  = true;   // present tile (x=5,  y=10)
    panel.setWdt(wdt);

    CHECK(panel.tilePresent(30, 32));
    CHECK(panel.tilePresent(5, 10));
    CHECK(!panel.tilePresent(0, 0));      // absent
    CHECK(!panel.tilePresent(-1, 0));     // out of range
    CHECK(!panel.tilePresent(64, 64));    // out of range

    // --- selectTile on a present tile edge-triggers a request once ----------
    int tx = -1, ty = -1;
    CHECK(!panel.takeTileRequest(tx, ty));   // nothing pending yet
    panel.selectTile(30, 32);
    CHECK(panel.takeTileRequest(tx, ty));    // true once
    CHECK(tx == 30 && ty == 32);
    CHECK(!panel.takeTileRequest(tx, ty));   // and cleared

    // --- selectTile on an absent tile is a no-op ----------------------------
    panel.selectTile(0, 0);
    CHECK(!panel.takeTileRequest(tx, ty));

    // --- draw() smoke test under the headless ImGui harness -----------------
    ImGui::NewFrame();
    panel.draw();
    ImGui::Render();
}
