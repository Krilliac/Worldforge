#include "server/world_sim.hpp"

namespace wf {

uint64_t WorldSim::spawnCreature(uint32_t entry, uint32_t mapId, const Vec3& pos, float o) {
    SimObject obj;
    obj.guid = nextGuid_++;
    obj.entry = entry;
    obj.mapId = mapId;
    obj.pos = pos;
    obj.orientation = o;
    uint64_t guid = obj.guid;
    objects_[guid] = std::move(obj);
    return guid;
}

bool WorldSim::moveObject(uint64_t guid, const Vec3& pos, float o) {
    auto it = objects_.find(guid);
    if (it == objects_.end()) return false;
    it->second.pos = pos;
    it->second.orientation = o;
    return true;
}

bool WorldSim::despawn(uint64_t guid) {
    return objects_.erase(guid) > 0;
}

bool WorldSim::setWaypoints(uint64_t guid, const std::vector<Vec3>& path) {
    auto it = objects_.find(guid);
    if (it == objects_.end()) return false;
    it->second.waypoints = path;
    return true;
}

const SimObject* WorldSim::find(uint64_t guid) const {
    auto it = objects_.find(guid);
    return it == objects_.end() ? nullptr : &it->second;
}

std::vector<uint64_t> WorldSim::guids() const {
    std::vector<uint64_t> out;
    out.reserve(objects_.size());
    for (const auto& kv : objects_) out.push_back(kv.first);
    return out;
}

} // namespace wf
