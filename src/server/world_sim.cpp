#include "server/world_sim.hpp"

#include <algorithm>
#include <cmath>

#include "editor_bridge.hpp"

namespace wf {

uint64_t WorldSim::spawnCreature(uint32_t entry, uint32_t mapId, const Vec3& pos, float o) {
    SimObject obj;
    obj.guid = nextGuid_++;
    obj.entry = entry;
    obj.mapId = mapId;
    obj.kind = EntityKind::Creature;
    obj.pos = pos;
    obj.orientation = o;
    uint64_t guid = obj.guid;
    objects_[guid] = std::move(obj);
    return guid;
}

uint64_t WorldSim::spawnPlayer(uint32_t mapId, const Vec3& pos, float o, const std::string& name) {
    SimObject obj;
    obj.guid = nextPlayerGuid_++;
    obj.mapId = mapId;
    obj.kind = EntityKind::Player;
    obj.name = name;
    obj.pos = pos;
    obj.orientation = o;
    obj.speed = 7.0f;                 // player run speed
    obj.radius = 1.0f;               // a humanoid's selectable radius
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
    it->second.wpIndex = 0;            // restart the patrol from the first node
    return true;
}

bool WorldSim::setSpeed(uint64_t guid, float speed) {
    auto it = objects_.find(guid);
    if (it == objects_.end()) return false;
    it->second.speed = speed;
    return true;
}

bool WorldSim::setBounds(uint64_t guid, float radius) {
    auto it = objects_.find(guid);
    if (it == objects_.end()) return false;
    it->second.radius = radius;
    return true;
}

bool WorldSim::setModelBox(uint64_t guid, const Vec3& localMin, const Vec3& localMax) {
    auto it = objects_.find(guid);
    if (it == objects_.end()) return false;
    it->second.boundsMin = localMin;
    it->second.boundsMax = localMax;
    return true;
}

size_t WorldSim::tick(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    simTimeMs_ += static_cast<uint64_t>(dt * 1000.0f + 0.5f);

    // Expire debug markers whose ttl elapsed (TEMPSPAWN_TIMED_DESPAWN's analog).
    markers_.erase(std::remove_if(markers_.begin(), markers_.end(),
                       [this](const SimMarker& m) { return m.expireAtMs <= simTimeMs_; }),
                   markers_.end());

    size_t moved = 0;
    for (auto& kv : objects_) {
        SimObject& o = kv.second;
        if (o.waypoints.size() < 2) { o.moving = false; continue; }
        if (o.wpIndex >= o.waypoints.size()) o.wpIndex = 0;

        float budget = o.speed * dt;
        bool  did = false;
        Vec3  lastDir{};
        // Spend the whole step budget, crossing as many nodes as it reaches this
        // tick. The arrival guard caps zero-length segments so a degenerate path
        // (coincident nodes) can't spin forever.
        int guard = static_cast<int>(o.waypoints.size()) + 2;
        while (budget > 1e-5f && guard-- > 0) {
            Vec3  to   = o.waypoints[o.wpIndex] - o.pos;
            float dist = length(to);
            if (dist < 1e-4f) {                              // already on the node
                o.wpIndex = (o.wpIndex + 1) % o.waypoints.size();
                continue;
            }
            Vec3 dir = to * (1.0f / dist);
            lastDir = dir; did = true;
            if (dist <= budget) {                            // reach it, keep going
                o.pos = o.waypoints[o.wpIndex];
                budget -= dist;
                o.wpIndex = (o.wpIndex + 1) % o.waypoints.size();
            } else {
                o.pos = o.pos + dir * budget;
                budget = 0.0f;
            }
        }
        if (did) {
            o.orientation = std::atan2(lastDir.y, lastDir.x);   // face travel dir
            o.moving = true;
            ++moved;
        } else {
            o.moving = false;
        }
    }
    return moved;
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

// ---- bridge ops beyond the object store ----
void WorldSim::reloadGrid(uint32_t mapId, int32_t gx, int32_t gy) {
    reloadedGrids_.push_back({ mapId, gx, gy });
}

void WorldSim::markPoints(const std::vector<Vec3>& points, uint32_t ttlMs) {
    markers_.reserve(markers_.size() + points.size());
    for (const Vec3& p : points)
        markers_.push_back({ p, simTimeMs_ + ttlMs });
}

void WorldSim::applySql(const std::string& sql, const std::string& reloadCommand) {
    appliedSql_.push_back({ sql, reloadCommand });
}

bool WorldSim::handleEditorFrame(const EditorFrame& frame, Ack& ack) {
    ack = Ack{};
    switch (frame.opcode) {
        case EDITOR_RELOAD_GRID: {
            ReloadGrid op;
            if (!decodeReloadGrid(frame.payload, op)) { ack.status = 1; return true; }
            ack.opId = op.opId;
            reloadGrid(op.mapId, op.gx, op.gy);
            return true;
        }
        case EDITOR_MARK_POINTS: {
            MarkPoints op;
            if (!decodeMarkPoints(frame.payload, op)) { ack.status = 1; return true; }
            ack.opId = op.opId;
            markPoints(op.points, op.ttlMs);       // zero points: a valid no-op
            return true;
        }
        case EDITOR_SQL_APPLY: {
            SqlApply op;
            if (!decodeSqlApply(frame.payload, op)) { ack.status = 1; return true; }
            ack.opId = op.opId;
            applySql(op.sql, op.reloadCommand);
            return true;
        }
        default:
            return false;                          // not ours: caller dispatches
    }
}

std::vector<SimObject> WorldSim::snapshot() const {
    std::vector<SimObject> out;
    out.reserve(objects_.size());
    for (const auto& kv : objects_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const SimObject& a, const SimObject& b){ return a.guid < b.guid; });
    return out;
}

} // namespace wf
