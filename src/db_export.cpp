#include "db_export.hpp"

#include <cmath>
#include <sstream>

namespace wf {

namespace {
// Compact, locale-independent float formatting for SQL literals.
std::string f(float v) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << v;
    return os.str();
}

// SQL string literal: single quotes doubled ('O'Neill' -> 'O''Neill') and
// backslashes doubled -- backslash is MySQL's escape character, so a lone one
// corrupts the value and a trailing one escapes the closing quote.
std::string quoted(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '\'';
    for (char c : s) {
        if      (c == '\'') out += "''";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    out += '\'';
    return out;
}
} // namespace

std::string creatureInsert(const CreatureSpawn& c) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << "INSERT INTO creature "
          "(guid, id, map, position_x, position_y, position_z, orientation, "
          "spawntimesecs, spawndist, MovementType) VALUES ("
       << c.guid << ", " << c.entry << ", " << c.mapId << ", "
       << f(c.pos.x) << ", " << f(c.pos.y) << ", " << f(c.pos.z) << ", "
       << f(c.orientation) << ", " << c.spawnTimeSecs << ", "
       << f(c.spawnDist) << ", " << c.movementType << ");";
    return os.str();
}

std::string gameObjectInsert(const GameObjectSpawn& g) {
    // mangos stores a quaternion about world +Z derived from orientation:
    // rotation2 = sin(o/2), rotation3 = cos(o/2); rotation0/1 = 0.
    const float rot2 = std::sin(g.orientation / 2.0f);
    const float rot3 = std::cos(g.orientation / 2.0f);
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << "INSERT INTO gameobject "
          "(guid, id, map, position_x, position_y, position_z, orientation, "
          "rotation0, rotation1, rotation2, rotation3, spawntimesecs, state) VALUES ("
       << g.guid << ", " << g.entry << ", " << g.mapId << ", "
       << f(g.pos.x) << ", " << f(g.pos.y) << ", " << f(g.pos.z) << ", "
       << f(g.orientation) << ", 0, 0, " << f(rot2) << ", " << f(rot3) << ", "
       << g.spawnTimeSecs << ", " << g.state << ");";
    return os.str();
}

std::string waypointInserts(uint32_t creatureGuid, const std::vector<Vec3>& path,
                            uint32_t waitTimeMs) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    // Mark the spawn as a waypoint mover so the path is used.
    os << "UPDATE creature SET MovementType = 2 WHERE guid = " << creatureGuid << ";\n";
    for (size_t i = 0; i < path.size(); ++i) {
        os << "INSERT INTO creature_movement "
              "(id, point, position_x, position_y, position_z, waittime) VALUES ("
           << creatureGuid << ", " << (i + 1) << ", "
           << f(path[i].x) << ", " << f(path[i].y) << ", " << f(path[i].z) << ", "
           << waitTimeMs << ");";
        if (i + 1 < path.size()) os << "\n";
    }
    return os.str();
}

std::string waypointSql(const WaypointPath& path, uint32_t idOrEntry, bool asTemplate) {
    const char* table = asTemplate ? "creature_movement_template" : "creature_movement";
    std::ostringstream os;
    os.imbue(std::locale::classic());
    // Delete-then-insert keeps the rewrite atomic: the whole path is always
    // re-emitted, so node removal/reorder renumbers Point for free.
    os << "DELETE FROM " << table << " WHERE Id = " << idOrEntry << ";";
    if (path.nodes.empty()) return os.str();     // empty path = clear the patrol

    os << "\nINSERT INTO " << table
       << " (Id, Point, PositionX, PositionY, PositionZ, Orientation, "
          "WaitTime, ScriptId, Comment) VALUES";
    for (size_t i = 0; i < path.nodes.size(); ++i) {
        const WaypointNode& n = path.nodes[i];
        os << "\n(" << idOrEntry << ", " << (i + 1) << ", "
           << f(n.pos.x) << ", " << f(n.pos.y) << ", " << f(n.pos.z) << ", "
           << f(n.orientation) << ", " << n.waitTimeMs << ", " << n.scriptId << ", "
           << quoted(n.comment) << ")"
           << (i + 1 < path.nodes.size() ? "," : ";");
    }
    return os.str();
}

std::string movementTypeUpdateSql(uint32_t guid, int movementType, bool nodel) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << "UPDATE creature SET MovementType = " << movementType
       << " WHERE guid = " << guid << ";";
    // Leaving waypoint motion normally drops the spawn's path rows; NODEL
    // keeps them so the patrol can be re-enabled later.
    if (movementType != 2 && !nodel) {
        os << "\nDELETE FROM creature_movement WHERE Id = " << guid << ";";
    }
    return os.str();
}

} // namespace wf
