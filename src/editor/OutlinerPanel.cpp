#include "editor/OutlinerPanel.hpp"

#include <cinttypes>
#include <cstdio>

#include "imgui.h"

namespace wf::editor {

const char* OutlinerPanel::entityKindName(uint8_t kind) {
    switch (kind) {
        case 0:  return "Creature";
        case 1:  return "Player";
        case 2:  return "GameObject";
        default: return "?";
    }
}

std::vector<OutlinerItem> OutlinerPanel::buildItems(const WorldView& view,
                                                    const TileScene* scene) {
    std::vector<OutlinerItem> items;

    // --- live entities (GUID-sorted snapshot from the view) -----------------
    for (const LiveEntity& le : view.entities()) {
        const EntityState& s = le.state;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Entity 0x%" PRIX64 " (%s)",
                      s.guid, entityKindName(s.kind));
        OutlinerItem it;
        it.kind  = OutlinerItem::Kind::Entity;
        it.label = buf;
        it.guid  = s.guid;
        it.index = 0;
        items.push_back(std::move(it));
    }

    if (!scene) return items;

    // --- placed M2 doodads (ADT MDDF) ---------------------------------------
    for (size_t i = 0; i < scene->instances.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Doodad #%zu", i);
        OutlinerItem it;
        it.kind  = OutlinerItem::Kind::Doodad;
        it.label = buf;
        it.guid  = 0;
        it.index = i;
        items.push_back(std::move(it));
    }

    // --- placed WMOs (ADT MODF) ---------------------------------------------
    for (size_t i = 0; i < scene->wmoInstances.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "WMO %u", scene->wmoInstances[i].uniqueId);
        OutlinerItem it;
        it.kind  = OutlinerItem::Kind::Wmo;
        it.label = buf;
        it.guid  = scene->wmoInstances[i].uniqueId;
        it.index = i;
        items.push_back(std::move(it));
    }

    return items;
}

OutlinerSelection OutlinerPanel::draw(const WorldView& view, const TileScene* scene) {
    OutlinerSelection result;  // kind None unless a row is clicked this frame

    ImGui::Begin("Outliner");

    const std::vector<OutlinerItem> items = buildItems(view, scene);

    // A stable per-row id, even when two rows share a label.
    auto emit = [&](const OutlinerItem& it, int row) {
        ImGui::PushID(row);
        if (ImGui::Selectable(it.label.c_str())) {
            switch (it.kind) {
                case OutlinerItem::Kind::Entity:
                    result.kind = OutlinerSelection::Kind::Entity; break;
                case OutlinerItem::Kind::Doodad:
                    result.kind = OutlinerSelection::Kind::Doodad; break;
                case OutlinerItem::Kind::Wmo:
                    result.kind = OutlinerSelection::Kind::Wmo;    break;
            }
            result.guid  = it.guid;
            result.index = it.index;
        }
        ImGui::PopID();
    };

    int row = 0;

    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool any = false;
        for (const OutlinerItem& it : items) {
            if (it.kind != OutlinerItem::Kind::Entity) continue;
            emit(it, row);
            any = true;
            ++row;
        }
        if (!any) ImGui::TextDisabled("(no live entities)");
    }

    if (ImGui::CollapsingHeader("Tile Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool any = false;
        for (const OutlinerItem& it : items) {
            if (it.kind == OutlinerItem::Kind::Entity) continue;
            emit(it, row);
            any = true;
            ++row;
        }
        if (!any) ImGui::TextDisabled("(no tile loaded)");
    }

    ImGui::End();
    return result;
}

} // namespace wf::editor
