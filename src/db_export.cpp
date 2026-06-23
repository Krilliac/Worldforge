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

} // namespace wf
