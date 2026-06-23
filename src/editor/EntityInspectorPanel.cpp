#include "editor/EntityInspectorPanel.hpp"

#include <cinttypes>
#include <cstdio>

#include "imgui.h"

namespace wf::editor {

const char* EntityInspectorPanel::kindName(uint8_t kind) {
    switch (kind) {
        case 0:  return "Creature";
        case 1:  return "Player";
        case 2:  return "GameObject";
        default: return "?";
    }
}

bool EntityInspectorPanel::passesFilter(const EntityState& s) const {
    switch (s.kind) {
        case 0: if (!showCreatures)   return false; break;
        case 1: if (!showPlayers)     return false; break;
        case 2: if (!showGameObjects) return false; break;
        default: break;
    }
    if (movingOnly && !s.moving) return false;
    return true;
}

const char* EntityInspectorPanel::sceneKindName(WorldPick::Kind kind) {
    switch (kind) {
        case WorldPick::Kind::Terrain: return "Terrain";
        case WorldPick::Kind::Doodad:  return "Doodad (M2)";
        case WorldPick::Kind::Wmo:     return "WMO";
        default:                       return "None";
    }
}

SpawnCreature EntityInspectorPanel::spawnOp(const Vec3& at) {
    SpawnCreature s;
    s.entry = static_cast<uint32_t>(spawnEntry);
    s.mapId = static_cast<uint32_t>(spawnMapId);
    s.pos = at; s.orientation = 0.0f; s.opId = nextOpId_++;
    return s;
}

Despawn EntityInspectorPanel::despawnOp(uint64_t guid) {
    Despawn d; d.guid = guid; d.opId = nextOpId_++;
    return d;
}

uint64_t EntityInspectorPanel::draw(WorldView& view, const WorldPick* sceneSel,
                                    std::vector<std::vector<uint8_t>>* ops) {
    ImGui::Begin("Entities");

    // --- a selected static scene object (terrain / doodad / WMO) -------------
    if (sceneSel && sceneSel->isScene()) {
        ImGui::TextUnformatted("Scene object selected:");
        ImGui::Text("Kind: %s   Index: %zu", sceneKindName(sceneSel->kind), sceneSel->index);
        if (sceneSel->kind == WorldPick::Kind::Wmo)
            ImGui::Text("WMO uniqueId: %u", sceneSel->uniqueId);
        ImGui::Text("Hit: (%.2f, %.2f, %.2f)  dist %.1f",
                    sceneSel->point.x, sceneSel->point.y, sceneSel->point.z, sceneSel->distance);
        ImGui::Separator();
    }

    // --- live summary (server-authoritative counts the engine mirrors) ------
    WorldView::Counts c = view.counts();
    ImGui::Text("Live: %zu   Creatures: %zu   Players: %zu   GO: %zu   Moving: %zu",
                c.total, c.creatures, c.players, c.gameObjects, c.moving);

    // --- server runtime status (the FxLog the server is broadcasting) -------
    if (view.hasServerStatus() && ImGui::CollapsingHeader("Server")) {
        const ServerStatus& s = view.serverStatus();
        ImGui::Text("Clock: %.1fs   Entities: %u",
                    static_cast<double>(s.simTimeMs) / 1000.0, s.entityCount);
        ImGui::Text("Weather: type %u @ %.2f   (%u sent)",
                    s.weatherType, s.weatherGrade, s.weatherCount);
        ImGui::Text("Sound: last %u (%u)   Cinematic: last %u (%u)",
                    s.lastSound, s.soundCount, s.lastCinematic, s.cinematicCount);
        ImGui::Text("Light: last %u (%u)   WorldState ops: %u",
                    s.lastOverrideLight, s.lightCount, s.worldStateCount);
        ImGui::Separator();
    }

    ImGui::Checkbox("Creatures", &showCreatures);  ImGui::SameLine();
    ImGui::Checkbox("Players",   &showPlayers);    ImGui::SameLine();
    ImGui::Checkbox("Objects",   &showGameObjects);ImGui::SameLine();
    ImGui::Checkbox("Moving only", &movingOnly);
    ImGui::Separator();

    const uint64_t selected = view.selected();

    // --- the entity table ---------------------------------------------------
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    int visible = 0;
    if (ImGui::BeginTable("entities", 6, flags, ImVec2(0, 220))) {
        ImGui::TableSetupColumn("GUID");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Entry");
        ImGui::TableSetupColumn("Map");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Mov");
        ImGui::TableHeadersRow();

        for (const LiveEntity& le : view.entities()) {
            const EntityState& s = le.state;
            if (!passesFilter(s)) continue;
            ++visible;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char guidStr[24];
            std::snprintf(guidStr, sizeof(guidStr), "%016" PRIX64, s.guid);
            // A row-spanning selectable drives the shared selection.
            if (ImGui::Selectable(guidStr, s.guid == selected,
                                  ImGuiSelectableFlags_SpanAllColumns))
                view.select(s.guid);

            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(kindName(s.kind));
            ImGui::TableSetColumnIndex(2); ImGui::Text("%u", s.entry);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%u", s.mapId);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f, %.1f, %.1f", s.pos.x, s.pos.y, s.pos.z);
            ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(s.moving ? "yes" : "");
        }
        ImGui::EndTable();
    }
    lastVisible_ = visible;

    // --- detail pane for the selected object --------------------------------
    ImGui::Separator();
    const LiveEntity* sel = view.hasSelection() ? view.find(view.selected()) : nullptr;
    if (sel) {
        const EntityState& s = sel->state;
        ImGui::Text("Selected: %016" PRIX64, s.guid);
        if (!s.name.empty()) ImGui::Text("Name: %s", s.name.c_str());
        ImGui::Text("Kind: %s   Entry: %u   Map: %u", kindName(s.kind), s.entry, s.mapId);
        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", s.pos.x, s.pos.y, s.pos.z);
        ImGui::Text("Facing: %.3f rad   Speed: %.2f   Moving: %s",
                    s.orientation, s.speed, s.moving ? "yes" : "no");
        ImGui::Text("Updates: %u   Last seen: %u ms", sel->updates, sel->lastSeenMs);
    } else {
        ImGui::TextDisabled("No selection -- click an entity above.");
    }

    // --- authoring: spawn at the picked point / despawn the selection --------
    if (ops) {
        ImGui::Separator();
        ImGui::TextUnformatted("Authoring");
        ImGui::InputInt("Entry", &spawnEntry);
        ImGui::InputInt("Map",   &spawnMapId);

        const bool canSpawn = sceneSel && sceneSel->hit();
        if (!canSpawn) ImGui::BeginDisabled();
        if (ImGui::Button("Spawn at pick") && canSpawn)
            ops->push_back(encode(spawnOp(sceneSel->point)));
        if (!canSpawn) ImGui::EndDisabled();
        if (canSpawn)
            ImGui::SameLine(), ImGui::Text("(%.0f, %.0f, %.0f)",
                                           sceneSel->point.x, sceneSel->point.y, sceneSel->point.z);

        const bool canDespawn = view.hasSelection();
        if (!canDespawn) ImGui::BeginDisabled();
        if (ImGui::Button("Despawn selected") && canDespawn)
            ops->push_back(encode(despawnOp(view.selected())));
        if (!canDespawn) ImGui::EndDisabled();
    }

    ImGui::End();
    return view.selected();
}

} // namespace wf::editor
