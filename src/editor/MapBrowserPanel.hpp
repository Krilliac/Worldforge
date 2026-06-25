#pragma once
// ---------------------------------------------------------------------------
// MapBrowserPanel: pick a map from the mounted client's catalog, then click a
// tile on the 64x64 ADT grid to request loading it. The host owns the actual
// IO: when the user selects a different map the panel edge-triggers a "map
// changed" event (takeMapChanged); the host loads that map's WDT and feeds the
// tile-presence back via setWdt(). Clicking a present tile edge-triggers a tile
// load request (takeTileRequest) the host turns into an ADT load.
//
// The selection logic (map change + tile pick) is separated from the ImGui
// draw() so it is unit-tested headless: draw() is the thin widget layer that
// maps a grid click into selectTile().
// ---------------------------------------------------------------------------
#include <array>
#include <string>
#include <vector>

#include "asset_catalog.hpp"   // MapInfo
#include "wow_files.hpp"       // Wdt

namespace wf::editor {

class MapBrowserPanel {
public:
    // Supply the browseable map catalog (from listMaps()).
    void setMaps(std::vector<MapInfo> maps);
    // Supply the selected map's tile-presence (the loaded WDT).
    void setWdt(const Wdt& wdt);

    // ImGui panel: window title "Map Browser". A scrollable map list, then a
    // 64x64 tile grid; clicking a present tile calls selectTile().
    void draw();

    // Edge-triggered: returns true ONCE after the user picks a different map,
    // outputting its name; host then loads that WDT and calls setWdt().
    bool takeMapChanged(std::string& outMapName);
    // Edge-triggered: returns true ONCE when a present tile was clicked -> (x,y).
    bool takeTileRequest(int& x, int& y);

    // Programmatic tile pick (also called by draw on a grid click) -- no-op if
    // the tile is absent in the current Wdt; sets the pending tile request.
    void selectTile(int x, int y);
    bool tilePresent(int x, int y) const;        // from the stored Wdt

    int  selectedMap() const { return selectedMap_; }

    // UI state (public so tests can set them):
    int selectedMap_ = -1;

private:
    std::vector<MapInfo> maps_;
    std::array<bool, 64 * 64> tiles_{};          // copied from setWdt
    bool mapChanged_ = false; std::string changedName_;
    bool tileReq_ = false; int tileX_ = 0, tileY_ = 0;
};

} // namespace wf::editor
