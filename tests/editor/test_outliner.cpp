#include "test.hpp"
#include "imgui.h"

#include "editor/OutlinerPanel.hpp"
#include "world_view.hpp"
#include "asset_loader.hpp"

#include <string>

using namespace wf;
using namespace wf::editor;

namespace {
EntityState mk(uint64_t guid, uint8_t kind, Vec3 pos) {
    EntityState e; e.guid = guid; e.kind = kind; e.entry = 200; e.mapId = 1;
    e.pos = pos; return e;
}
} // namespace

void test_outliner() {
    std::printf("[editor.outliner]\n");

    // --- a live view with two entities --------------------------------------
    WorldView view;
    view.apply(mk(0xABCD, 0, {0, 0, 0}));   // creature
    view.apply(mk(0x1234, 1, {5, 0, 0}));   // player

    // --- a tile with two doodads + one WMO ----------------------------------
    TileScene scene;
    scene.meshes.push_back(TexMesh{});      // one mesh shared by the doodads
    scene.instances.push_back(TileScene::Inst{ 0, 0, Mat4{}, false, {0,0,0} });
    scene.instances.push_back(TileScene::Inst{ 0, 0, Mat4{}, false, {0,0,0} });
    scene.wmoInstances.push_back(TileScene::WmoInst{ Mesh{}, Mat4{}, 12345u });

    // --- buildItems(): entities first, then doodads, then WMOs --------------
    auto items = OutlinerPanel::buildItems(view, &scene);
    CHECK(items.size() == 5);               // 2 entities + 2 doodads + 1 wmo

    // The two entity rows (GUID-sorted: 0x1234 < 0xABCD).
    CHECK(items[0].kind == OutlinerItem::Kind::Entity);
    CHECK(items[1].kind == OutlinerItem::Kind::Entity);
    CHECK(items[0].guid == 0x1234ull);      // player sorts first
    CHECK(items[1].guid == 0xABCDull);      // creature second
    CHECK(items[0].label.find("Player") != std::string::npos);
    CHECK(items[1].label.find("Creature") != std::string::npos);

    // The two doodad rows carry their vector index and guid 0.
    CHECK(items[2].kind == OutlinerItem::Kind::Doodad);
    CHECK(items[3].kind == OutlinerItem::Kind::Doodad);
    CHECK(items[2].index == 0);
    CHECK(items[3].index == 1);
    CHECK(items[2].guid == 0);

    // The WMO row carries its uniqueId (as guid) and index.
    CHECK(items[4].kind == OutlinerItem::Kind::Wmo);
    CHECK(items[4].guid == 12345ull);       // WMO uniqueId
    CHECK(items[4].index == 0);
    CHECK(items[4].label.find("12345") != std::string::npos);

    // --- no scene: only the entities are listed -----------------------------
    auto noScene = OutlinerPanel::buildItems(view, nullptr);
    CHECK(noScene.size() == 2);
    CHECK(noScene[0].kind == OutlinerItem::Kind::Entity);
    CHECK(noScene[1].kind == OutlinerItem::Kind::Entity);

    // --- entityKindName mapping ---------------------------------------------
    CHECK(std::string(OutlinerPanel::entityKindName(0)) == "Creature");
    CHECK(std::string(OutlinerPanel::entityKindName(1)) == "Player");
    CHECK(std::string(OutlinerPanel::entityKindName(2)) == "GameObject");

    // --- headless draw: valid ImGui geometry, no selection on a quiet frame --
    {
        OutlinerPanel panel;
        ImGui::NewFrame();
        OutlinerSelection sel = panel.draw(view, &scene);
        ImGui::Render();
        CHECK(ImGui::GetDrawData() != nullptr && ImGui::GetDrawData()->Valid);
        CHECK(sel.kind == OutlinerSelection::Kind::None);   // nothing clicked
    }
}
