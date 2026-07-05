#include "editor/WaypointPanel.hpp"

#include <cstdio>

#include "imgui.h"

#include "db_export.hpp"

namespace wf::editor {

void WaypointPanel::addNodeAt(const Vec3& p) {
    WaypointNode n;
    n.pos = p;
    appendNode(path, std::move(n));
    selected = static_cast<int>(path.nodes.size()) - 1;
}

SetWaypoints WaypointPanel::previewOp() {
    SetWaypoints s;
    s.guid = targetGuid;
    s.path.reserve(path.nodes.size());
    for (const WaypointNode& n : path.nodes) s.path.push_back(n.pos);
    s.opId = nextOpId_++;
    return s;
}

std::string WaypointPanel::currentSql() const {
    const uint32_t idOrEntry = asTemplate ? templateEntry
                                          : static_cast<uint32_t>(targetGuid);
    return waypointSql(path, idOrEntry, asTemplate);
}

void WaypointPanel::draw() {
    ImGui::Begin("Waypoints");

    // --- target + storage tier ---
    ImGui::InputScalar("Spawn GUID", ImGuiDataType_U64, &targetGuid,
                       nullptr, nullptr, "%llu");
    ImGui::InputScalar("Entry", ImGuiDataType_U32, &templateEntry);
    if (ImGui::RadioButton("This spawn only (guid)", !asTemplate)) asTemplate = false;
    if (ImGui::RadioButton("All spawns of entry (template)", asTemplate)) asTemplate = true;
    ImGui::Separator();

    // --- node list: "1 (wait 5000ms)" rows with select / delete -------------
    ImGui::Text("%d node(s) -- click terrain to append", (int)path.nodes.size());
    int deleteAt = -1;
    for (int i = 0; i < (int)path.nodes.size(); ++i) {
        const WaypointNode& n = path.nodes[i];
        char label[64];
        if (n.waitTimeMs > 0)
            std::snprintf(label, sizeof(label), "%d (wait %ums)##wp%d", i + 1, n.waitTimeMs, i);
        else
            std::snprintf(label, sizeof(label), "%d##wp%d", i + 1, i);
        if (ImGui::Selectable(label, selected == i)) selected = i;
        ImGui::SameLine();
        char del[32];
        std::snprintf(del, sizeof(del), "X##del%d", i);
        if (ImGui::SmallButton(del)) deleteAt = i;
    }
    if (deleteAt >= 0) {
        removeNode(path, (size_t)deleteAt);          // Point renumber is implicit:
        if (selected >= (int)path.nodes.size())      // SQL re-emits the whole path
            selected = (int)path.nodes.size() - 1;
    }
    if (ImGui::Button("Reverse")) reverse(path);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { path.nodes.clear(); selected = -1; }
    ImGui::Separator();

    // --- selected node fields ------------------------------------------------
    if (selected >= 0 && selected < (int)path.nodes.size()) {
        WaypointNode& n = path.nodes[selected];
        ImGui::Text("Node %d  pos %.1f %.1f %.1f", selected + 1,
                    n.pos.x, n.pos.y, n.pos.z);                 // pos: viewport-authored
        ImGui::DragFloat("Orientation", &n.orientation, 0.05f, 0.0f, 6.2832f);
        int wait = (int)n.waitTimeMs;
        if (ImGui::InputInt("Wait (ms)", &wait)) n.waitTimeMs = wait < 0 ? 0u : (uint32_t)wait;
        int script = (int)n.scriptId;
        if (ImGui::InputInt("Script ID", &script)) n.scriptId = script < 0 ? 0u : (uint32_t)script;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", n.comment.c_str());
        if (ImGui::InputText("Comment", buf, sizeof(buf))) n.comment = buf;
        ImGui::Separator();
    }

    // --- persistence + live preview ------------------------------------------
    if (ImGui::Button("Copy SQL for path"))
        ImGui::SetClipboardText(currentSql().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Preview on server") && onPreview)
        onPreview(previewOp());

    // Blocked-segment report (the host feeds the same results to the overlay).
    if (losFn && path.nodes.size() >= 2) {
        int blocked = 0;
        for (const LosResult& r : validateSegments(path, losFn))
            if (r.blocked) ++blocked;
        if (blocked > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "%d segment(s) blocked (LoS)", blocked);
        else
            ImGui::Text("All segments clear (LoS)");
    }

    ImGui::End();
}

// ---- debugdraw feed ---------------------------------------------------------

std::vector<DebugMarker> buildMarkers(const WaypointPath& path) {
    std::vector<DebugMarker> out;
    out.reserve(path.nodes.size());
    for (size_t i = 0; i < path.nodes.size(); ++i) {
        DebugMarker m;
        m.type  = DebugVisType::Path;
        m.pos   = path.nodes[i].pos;
        m.color = {0, 200, 255, 255};
        m.value = static_cast<float>(i + 1);
        m.label = std::to_string(i + 1);        // numbered "1", "2", ...
        out.push_back(std::move(m));
    }
    return out;
}

DebugPath buildPathLine(const WaypointPath& path, uint64_t guid) {
    DebugPath p;
    p.guid  = guid;
    p.color = {0, 200, 255, 255};
    p.points.reserve(path.nodes.size());
    for (const WaypointNode& n : path.nodes) p.points.push_back(n.pos);
    return p;
}

std::vector<DebugPath> buildBlockedOverlays(const WaypointPath& path,
                                            const std::vector<LosResult>& results) {
    std::vector<DebugPath> out;
    for (const LosResult& r : results) {
        if (!r.blocked) continue;
        const size_t i = static_cast<size_t>(r.fromIdx);
        if (i + 1 >= path.nodes.size()) continue;
        DebugPath p;
        p.bad   = true;
        p.color = {255, 0, 0, 255};             // blocked = red
        p.points = { path.nodes[i].pos, path.nodes[i + 1].pos };
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace wf::editor
