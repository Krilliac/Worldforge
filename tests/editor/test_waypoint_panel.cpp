#include "test.hpp"
#include "imgui.h"

#include "editor/WaypointPanel.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace wf;
using namespace wf::editor;

void test_waypoint_panel() {
    std::printf("[editor.waypoint_panel]\n");

    // --- addNodeAt appends (the viewport click-to-append workflow) ----------
    WaypointPanel wp;
    wp.targetGuid = 50001;
    wp.templateEntry = 299;
    wp.addNodeAt({0.0f, 0.0f, 0.0f});
    wp.addNodeAt({10.0f, 0.0f, 0.0f});
    wp.addNodeAt({20.0f, 0.0f, 0.0f});
    CHECK(wp.path.nodes.size() == 3);
    CHECK(wp.selected == 2);                       // last appended is selected
    CHECK_APPROX(wp.path.nodes[1].pos.x, 10.0f);

    // --- buildMarkers: N numbered markers + a polyline with N points --------
    {
        std::vector<DebugMarker> markers = buildMarkers(wp.path);
        CHECK(markers.size() == 3);
        CHECK(markers[0].label == "1");
        CHECK(markers[1].label == "2");
        CHECK(markers[2].label == "3");
        CHECK_APPROX(markers[2].pos.x, 20.0f);

        DebugPath line = buildPathLine(wp.path, wp.targetGuid);
        CHECK(line.points.size() == 3);
        CHECK(line.guid == 50001);
        CHECK(!line.bad);

        // Markers + polyline land in a DebugDraw like the server debug stream.
        DebugDraw dd;
        for (const DebugMarker& m : markers) apply(dd, m);
        apply(dd, line);
        CHECK(dd.stats().points > 0 && dd.stats().lines > 0);
    }

    // --- blocked segments come back as red overlay paths --------------------
    {
        wp.losFn = [](Vec3 a, Vec3 b) {            // block only node1 -> node2
            return !(a.x == 10.0f && b.x == 20.0f);
        };
        std::vector<LosResult> res = validateSegments(wp.path, wp.losFn);
        std::vector<DebugPath> overlays = buildBlockedOverlays(wp.path, res);
        CHECK(overlays.size() == 1);
        CHECK(overlays[0].bad);
        CHECK(overlays[0].color.r == 255 && overlays[0].color.g == 0);
        CHECK(overlays[0].points.size() == 2);
        CHECK_APPROX(overlays[0].points[0].x, 10.0f);
        CHECK_APPROX(overlays[0].points[1].x, 20.0f);
    }

    // --- preview: the SetWaypoints op carries the full path ------------------
    {
        SetWaypoints captured;
        int calls = 0;
        wp.onPreview = [&](const SetWaypoints& s) { captured = s; ++calls; };
        // What the "Preview on server" button runs:
        if (wp.onPreview) wp.onPreview(wp.previewOp());
        CHECK(calls == 1);
        CHECK(captured.guid == 50001);
        CHECK(captured.path.size() == 3);
        CHECK_APPROX(captured.path[2].x, 20.0f);
        CHECK(captured.opId == 1);
        CHECK(wp.previewOp().opId == 2);           // opId sequence advances
    }

    // --- guid/template radio switches the emitted SQL table -----------------
    {
        wp.asTemplate = false;
        std::string guidSql = wp.currentSql();
        CHECK(guidSql.find("creature_movement WHERE Id = 50001") != std::string::npos);
        CHECK(guidSql.find("creature_movement_template") == std::string::npos);

        wp.asTemplate = true;
        std::string tmplSql = wp.currentSql();
        CHECK(tmplSql.find("creature_movement_template WHERE Id = 299") != std::string::npos);
    }

    // --- headless ImGui: the panel draws without a backend ------------------
    {
        wp.path.nodes[0].waitTimeMs = 5000;        // exercises the "(wait ...)" row
        ImGui::NewFrame();
        wp.draw();
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();
        CHECK(dd != nullptr && dd->Valid);
    }
}
