#include "test.hpp"
#include "imgui.h"

#include "editor/EntityInspectorPanel.hpp"
#include "world_view.hpp"
#include "editor_bridge.hpp"

#include <cstring>
#include <string>

using namespace wf;
using namespace wf::editor;

namespace {
EntityState mk(uint64_t guid, uint8_t kind, Vec3 pos, bool moving) {
    EntityState e; e.guid = guid; e.kind = kind; e.entry = 200; e.mapId = 1;
    e.pos = pos; e.moving = moving; e.speed = 5.0f; return e;
}
} // namespace

void test_entity_inspector() {
    std::printf("[editor.entity_inspector]\n");

    WorldView view;
    view.apply(mk(10, 0, {0,0,0}, true));     // creature, moving
    view.apply(mk(20, 1, {5,0,0}, false));    // player, idle
    view.apply(mk(30, 2, {0,5,0}, true));     // gameobject, moving

    EntityInspectorPanel panel;

    // --- pure filter logic ---------------------------------------------------
    CHECK(panel.passesFilter(view.find(10)->state));
    panel.showCreatures = false;
    CHECK(!panel.passesFilter(view.find(10)->state));   // creature hidden
    CHECK(panel.passesFilter(view.find(20)->state));    // player still shown
    panel.showCreatures = true;

    panel.movingOnly = true;
    CHECK(panel.passesFilter(view.find(10)->state));    // moving creature
    CHECK(!panel.passesFilter(view.find(20)->state));   // idle player filtered
    panel.movingOnly = false;

    CHECK(std::string(EntityInspectorPanel::kindName(0)) == "Creature");
    CHECK(std::string(EntityInspectorPanel::kindName(1)) == "Player");
    CHECK(std::string(EntityInspectorPanel::kindName(2)) == "GameObject");

    // --- headless draw: produces valid ImGui geometry, no backend -----------
    {
        ImGui::NewFrame();
        uint64_t sel = panel.draw(view);
        ImGui::Render();
        CHECK(ImGui::GetDrawData() != nullptr && ImGui::GetDrawData()->Valid);
        CHECK(sel == 0);                       // nothing selected yet
    }

    // --- selection flows through the shared WorldView -----------------------
    view.select(30);
    {
        ImGui::NewFrame();
        uint64_t sel = panel.draw(view);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
        CHECK(sel == 30);                      // panel reports the selection
    }

    // The detail pane has data to show for the selection.
    CHECK(view.hasSelection());
    CHECK(view.find(view.selected())->state.kind == 2);

    // Drawing after the selected entity leaves doesn't crash and clears it.
    view.remove(30);
    {
        ImGui::NewFrame();
        uint64_t sel = panel.draw(view);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
        CHECK(sel == 0);                       // selection auto-cleared on remove
    }

    // --- a selected static scene object renders its detail section ----------
    CHECK(std::string(EntityInspectorPanel::sceneKindName(WorldPick::Kind::Wmo)) == "WMO");
    CHECK(std::string(EntityInspectorPanel::sceneKindName(WorldPick::Kind::Terrain)) == "Terrain");
    {
        WorldPick sceneSel;
        sceneSel.kind = WorldPick::Kind::Wmo; sceneSel.uniqueId = 2001;
        sceneSel.point = {12, 34, 56}; sceneSel.distance = 7.5f;
        CHECK(sceneSel.isScene());
        ImGui::NewFrame();
        panel.draw(view, &sceneSel);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);    // scene-object section drew fine
    }

    // --- authoring op builders ----------------------------------------------
    panel.spawnEntry = 299; panel.spawnMapId = 1;
    SpawnCreature sp = panel.spawnOp({10, 20, 30});
    CHECK(sp.entry == 299 && sp.mapId == 1);
    CHECK_APPROX(sp.pos.y, 20.0f);
    CHECK(sp.opId == 1);
    Despawn dp = panel.despawnOp(0xF130000000000005ull);
    CHECK(dp.guid == 0xF130000000000005ull && dp.opId == 2);   // opId sequences

    // The authoring section draws (with an ops sink) without crashing.
    {
        WorldPick sceneSel; sceneSel.kind = WorldPick::Kind::Terrain;
        sceneSel.point = {1,2,3};
        std::vector<std::vector<uint8_t>> ops;
        ImGui::NewFrame();
        panel.draw(view, &sceneSel, &ops);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
    }
}
