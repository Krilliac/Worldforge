#include "test.hpp"
#include "waypoint_path.hpp"
#include "db_export.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace wf;

namespace {

// 4-node L: three segments, two along +X then one up +Y.
WaypointPath lPath() {
    WaypointPath p;
    p.pathId = 50001;
    appendNode(p, {{0.0f, 0.0f, 0.0f}, 0, 0, 0, ""});     // 1
    appendNode(p, {{10.0f, 0.0f, 0.0f}, 0, 0, 0, ""});    // 2
    appendNode(p, {{20.0f, 0.0f, 0.0f}, 0, 0, 0, ""});    // 3
    appendNode(p, {{20.0f, 10.0f, 0.0f}, 0, 0, 0, ""});   // 4
    return p;
}

} // namespace

void test_waypoint_path() {
    std::printf("[waypoint_path]\n");

    // --- insertNodeNearestSegment picks the right leg ------------------------
    {
        // Near the middle leg (segment 1: node1 -> node2): lands at index 2.
        WaypointPath p = lPath();
        size_t at = insertNodeNearestSegment(p, {15.0f, 1.0f, 0.0f});
        CHECK(at == 2);
        CHECK(p.nodes.size() == 5);
        CHECK_APPROX(p.nodes[2].pos.x, 15.0f);
        // Order preserved around the split.
        CHECK_APPROX(p.nodes[1].pos.x, 10.0f);
        CHECK_APPROX(p.nodes[3].pos.x, 20.0f);
    }
    {
        // Near the vertical leg (segment 2: node2 -> node3): lands at index 3.
        WaypointPath p = lPath();
        size_t at = insertNodeNearestSegment(p, {21.0f, 5.0f, 0.0f});
        CHECK(at == 3);
        CHECK_APPROX(p.nodes[3].pos.y, 5.0f);
    }
    {
        // Empty / 1-node paths just append.
        WaypointPath p;
        CHECK(insertNodeNearestSegment(p, {1, 2, 3}) == 0);
        CHECK(insertNodeNearestSegment(p, {4, 5, 6}) == 1);
        CHECK(p.nodes.size() == 2);
    }

    // --- move / remove / reverse ---------------------------------------------
    {
        WaypointPath p = lPath();
        moveNode(p, 1, {11.0f, 1.0f, 2.0f});
        CHECK_APPROX(p.nodes[1].pos.z, 2.0f);
        reverse(p);
        CHECK_APPROX(p.nodes[0].pos.y, 10.0f);     // old tail is the new head
        CHECK_APPROX(p.nodes[3].pos.x, 0.0f);
    }

    // --- removeNode(0): renumber shows up in the emitted Point values --------
    {
        WaypointPath p = lPath();
        removeNode(p, 0);                          // old node 2 becomes Point 1
        std::string sql = waypointSql(p, 50001, false);
        CHECK(sql.find("(50001, 1, 10, 0, 0,") != std::string::npos);
        CHECK(sql.find("(50001, 3, 20, 10, 0,") != std::string::npos);
        CHECK(sql.find("(50001, 4,") == std::string::npos);   // only 3 rows left
    }

    // --- validateSegments: a fake LoS blocks exactly one leg ------------------
    {
        WaypointPath p = lPath();
        // Block only the segment leaving x=10 (node1 -> node2).
        auto losFn = [](Vec3 a, Vec3 b) {
            return !(a.x == 10.0f && b.x == 20.0f && b.y == 0.0f);
        };
        std::vector<LosResult> r = validateSegments(p, losFn);
        CHECK(r.size() == 3);                      // one verdict per segment
        CHECK(r[0].fromIdx == 0 && !r[0].blocked);
        CHECK(r[1].fromIdx == 1 && r[1].blocked);
        CHECK(r[2].fromIdx == 2 && !r[2].blocked);

        // No segments -> no results.
        WaypointPath solo;
        appendNode(solo, {{1, 1, 1}, 0, 0, 0, ""});
        CHECK(validateSegments(solo, losFn).empty());
    }

    // --- waypointSql golden strings: guid tier --------------------------------
    {
        WaypointPath p;
        appendNode(p, {{1.0f, 2.0f, 3.0f}, 0.0f, 5000, 0, "O'Neill"});
        appendNode(p, {{4.0f, 5.0f, 6.0f}, 1.5f, 0, 12, ""});
        std::string sql = waypointSql(p, 50001, false);
        CHECK(sql ==
              "DELETE FROM creature_movement WHERE Id = 50001;\n"
              "INSERT INTO creature_movement (Id, Point, PositionX, PositionY, "
              "PositionZ, Orientation, WaitTime, ScriptId, Comment) VALUES\n"
              "(50001, 1, 1, 2, 3, 0, 5000, 0, 'O''Neill'),\n"
              "(50001, 2, 4, 5, 6, 1.5, 0, 12, '');");
        // Atomic shape: the DELETE precedes the single bulk INSERT.
        CHECK(sql.find("DELETE FROM") < sql.find("INSERT INTO"));
        CHECK(sql.find("INSERT INTO", sql.find("INSERT INTO") + 1) == std::string::npos);
    }

    // --- waypointSql golden strings: template tier -----------------------------
    {
        WaypointPath p;
        appendNode(p, {{7.0f, 8.0f, 9.0f}, 0.0f, 0, 0, "start"});
        std::string sql = waypointSql(p, 299, true);
        CHECK(sql ==
              "DELETE FROM creature_movement_template WHERE Id = 299;\n"
              "INSERT INTO creature_movement_template (Id, Point, PositionX, "
              "PositionY, PositionZ, Orientation, WaitTime, ScriptId, Comment) VALUES\n"
              "(299, 1, 7, 8, 9, 0, 0, 0, 'start');");
    }

    // --- empty path = just the DELETE (clears the patrol) ---------------------
    {
        WaypointPath p;
        CHECK(waypointSql(p, 50001, false) ==
              "DELETE FROM creature_movement WHERE Id = 50001;");
        CHECK(waypointSql(p, 299, true) ==
              "DELETE FROM creature_movement_template WHERE Id = 299;");
    }

    // --- movementTypeUpdateSql: NODEL keeps the rows ---------------------------
    {
        // Switch away from waypoint motion: default also drops the path rows.
        std::string away = movementTypeUpdateSql(50001, 0, false);
        CHECK(away ==
              "UPDATE creature SET MovementType = 0 WHERE guid = 50001;\n"
              "DELETE FROM creature_movement WHERE Id = 50001;");
        // NODEL: the UPDATE only, path rows preserved.
        std::string nodel = movementTypeUpdateSql(50001, 1, true);
        CHECK(nodel == "UPDATE creature SET MovementType = 1 WHERE guid = 50001;");
        CHECK(nodel.find("DELETE") == std::string::npos);
        // Switching TO waypoint never deletes (the path SQL owns the rows).
        std::string to = movementTypeUpdateSql(50001, 2, false);
        CHECK(to == "UPDATE creature SET MovementType = 2 WHERE guid = 50001;");
    }
}
