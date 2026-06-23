#include "editor_bridge.hpp"
#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "worldproto.hpp"   // reuse the client-header framing convention

namespace wf {

namespace {
void putVec3(ByteWriter& w, const Vec3& v) { w.f32(v.x); w.f32(v.y); w.f32(v.z); }
Vec3 getVec3(ByteReader& r) { float x = r.f32(), y = r.f32(), z = r.f32(); return {x, y, z}; }

void putRgba(ByteWriter& w, Rgba c) { w.u8(c.r); w.u8(c.g); w.u8(c.b); w.u8(c.a); }
Rgba getRgba(ByteReader& r) { Rgba c; c.r = r.u8(); c.g = r.u8(); c.b = r.u8(); c.a = r.u8(); return c; }

void putStr(ByteWriter& w, const std::string& s) {
    uint16_t n = static_cast<uint16_t>(s.size() > 0xFFFF ? 0xFFFF : s.size());
    w.u16(n);
    w.bytes(reinterpret_cast<const uint8_t*>(s.data()), n);
}
std::string getStr(ByteReader& r) {
    uint16_t n = r.u16();
    std::string s(reinterpret_cast<const char*>(r.ptr()), n);
    r.skip(n);
    return s;
}
} // namespace

DebugCategory categoryFor(DebugVisType t) {
    switch (t) {
        case DebugVisType::Cell:      return DebugCategory::Cell;
        case DebugVisType::LosOk:
        case DebugVisType::LosBlock:  return DebugCategory::LineOfSight;
        case DebugVisType::Path:
        case DebugVisType::PathBad:   return DebugCategory::NavPath;
        case DebugVisType::Collision: return DebugCategory::Collision;
        case DebugVisType::HitPoint:  return DebugCategory::HitPoint;
        case DebugVisType::Height:    return DebugCategory::Height;
        case DebugVisType::Generic:   break;
    }
    return DebugCategory::Generic;
}

std::vector<uint8_t> frame(uint32_t opcode, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out = writeClientHeader(opcode, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool readFrame(const std::vector<uint8_t>& buf, EditorFrame& out, size_t& consumed) {
    if (buf.size() < 6) return false;                       // need the 6-byte header
    ClientHeader h = readClientHeader(buf.data());
    const size_t total = 6 + h.payloadLen;
    if (buf.size() < total) return false;                   // payload not all here yet
    out.opcode = h.opcode;
    out.payload.assign(buf.begin() + 6, buf.begin() + total);
    consumed = total;
    return true;
}

// ---- encode ----
std::vector<uint8_t> encode(const MoveObject& m) {
    ByteWriter w; w.u64(m.guid); putVec3(w, m.pos); w.f32(m.orientation); w.u32(m.opId);
    return frame(EDITOR_MOVE_OBJECT, w.data());
}
std::vector<uint8_t> encode(const SpawnCreature& s) {
    ByteWriter w; w.u32(s.entry); w.u32(s.mapId); putVec3(w, s.pos); w.f32(s.orientation); w.u32(s.opId);
    return frame(EDITOR_SPAWN_CREATURE, w.data());
}
std::vector<uint8_t> encode(const Despawn& d) {
    ByteWriter w; w.u64(d.guid); w.u32(d.opId);
    return frame(EDITOR_DESPAWN, w.data());
}
std::vector<uint8_t> encode(const SetWaypoints& s) {
    ByteWriter w; w.u64(s.guid); w.u32(static_cast<uint32_t>(s.path.size()));
    for (const Vec3& p : s.path) putVec3(w, p);
    w.u32(s.opId);
    return frame(EDITOR_SET_WAYPOINTS, w.data());
}
std::vector<uint8_t> encode(const Ack& a) {
    ByteWriter w; w.u32(a.opId); w.u8(a.status);
    return frame(EDITOR_ACK, w.data());
}
std::vector<uint8_t> encode(const OverrideLight& o) {
    ByteWriter w;
    w.u32(o.overrideLightId); w.u32(o.fadeInMs);
    w.u8(static_cast<uint8_t>(o.scope)); w.u64(o.targetGuid); w.u32(o.zoneId); w.u32(o.opId);
    return frame(EDITOR_OVERRIDE_LIGHT, w.data());
}

// ---- decode ----
MoveObject decodeMoveObject(const std::vector<uint8_t>& p) {
    ByteReader r(p); MoveObject m;
    m.guid = r.u64(); m.pos = getVec3(r); m.orientation = r.f32(); m.opId = r.u32();
    return m;
}
SpawnCreature decodeSpawnCreature(const std::vector<uint8_t>& p) {
    ByteReader r(p); SpawnCreature s;
    s.entry = r.u32(); s.mapId = r.u32(); s.pos = getVec3(r); s.orientation = r.f32(); s.opId = r.u32();
    return s;
}
Despawn decodeDespawn(const std::vector<uint8_t>& p) {
    ByteReader r(p); Despawn d; d.guid = r.u64(); d.opId = r.u32(); return d;
}
SetWaypoints decodeSetWaypoints(const std::vector<uint8_t>& p) {
    ByteReader r(p); SetWaypoints s;
    s.guid = r.u64();
    uint32_t n = r.u32();
    s.path.reserve(n);
    for (uint32_t i = 0; i < n; ++i) s.path.push_back(getVec3(r));
    s.opId = r.u32();
    return s;
}
Ack decodeAck(const std::vector<uint8_t>& p) {
    ByteReader r(p); Ack a; a.opId = r.u32(); a.status = r.u8(); return a;
}
OverrideLight decodeOverrideLight(const std::vector<uint8_t>& p) {
    ByteReader r(p); OverrideLight o;
    o.overrideLightId = r.u32(); o.fadeInMs = r.u32();
    o.scope = static_cast<FxScope>(r.u8()); o.targetGuid = r.u64();
    o.zoneId = r.u32(); o.opId = r.u32();
    return o;
}

// ---- debug stream encode ----
std::vector<uint8_t> encode(const DebugMarker& m) {
    ByteWriter w; w.u8(static_cast<uint8_t>(m.type)); putVec3(w, m.pos);
    putRgba(w, m.color); w.f32(m.value); putStr(w, m.label);
    return frame(EDITOR_DEBUG_MARKER, w.data());
}
std::vector<uint8_t> encode(const DebugLine& l) {
    ByteWriter w; w.u8(static_cast<uint8_t>(l.type)); putVec3(w, l.from); putVec3(w, l.to);
    putRgba(w, l.color); w.u8(l.hasHit ? 1 : 0); putVec3(w, l.hit);
    return frame(EDITOR_DEBUG_LINE, w.data());
}
std::vector<uint8_t> encode(const DebugPath& p) {
    ByteWriter w; w.u64(p.guid); w.u8(p.bad ? 1 : 0); putRgba(w, p.color);
    w.u32(static_cast<uint32_t>(p.points.size()));
    for (const Vec3& v : p.points) putVec3(w, v);
    return frame(EDITOR_DEBUG_PATH, w.data());
}
std::vector<uint8_t> encode(const DebugVolume& v) {
    ByteWriter w; w.u8(v.kind); w.u8(static_cast<uint8_t>(v.type));
    putVec3(w, v.center); putVec3(w, v.half); w.f32(v.radius); putRgba(w, v.color);
    return frame(EDITOR_DEBUG_VOLUME, w.data());
}

// ---- debug stream decode ----
DebugMarker decodeDebugMarker(const std::vector<uint8_t>& p) {
    ByteReader r(p); DebugMarker m;
    m.type = static_cast<DebugVisType>(r.u8()); m.pos = getVec3(r);
    m.color = getRgba(r); m.value = r.f32(); m.label = getStr(r);
    return m;
}
DebugLine decodeDebugLine(const std::vector<uint8_t>& p) {
    ByteReader r(p); DebugLine l;
    l.type = static_cast<DebugVisType>(r.u8()); l.from = getVec3(r); l.to = getVec3(r);
    l.color = getRgba(r); l.hasHit = r.u8() != 0; l.hit = getVec3(r);
    return l;
}
DebugPath decodeDebugPath(const std::vector<uint8_t>& p) {
    ByteReader r(p); DebugPath d;
    d.guid = r.u64(); d.bad = r.u8() != 0; d.color = getRgba(r);
    uint32_t n = r.u32(); d.points.reserve(n);
    for (uint32_t i = 0; i < n; ++i) d.points.push_back(getVec3(r));
    return d;
}
DebugVolume decodeDebugVolume(const std::vector<uint8_t>& p) {
    ByteReader r(p); DebugVolume v;
    v.kind = r.u8(); v.type = static_cast<DebugVisType>(r.u8());
    v.center = getVec3(r); v.half = getVec3(r); v.radius = r.f32(); v.color = getRgba(r);
    return v;
}

// ---- live entity stream encode / decode ----
std::vector<uint8_t> encode(const EntityState& e) {
    ByteWriter w;
    w.u64(e.guid); w.u8(e.kind); w.u32(e.entry); w.u32(e.mapId);
    putVec3(w, e.pos); w.f32(e.orientation); w.u8(e.moving ? 1 : 0); w.f32(e.speed);
    putStr(w, e.name);
    return frame(EDITOR_ENTITY_STATE, w.data());
}
std::vector<uint8_t> encode(const EntityRemove& e) {
    ByteWriter w; w.u64(e.guid);
    return frame(EDITOR_ENTITY_REMOVE, w.data());
}
EntityState decodeEntityState(const std::vector<uint8_t>& p) {
    ByteReader r(p); EntityState e;
    e.guid = r.u64(); e.kind = r.u8(); e.entry = r.u32(); e.mapId = r.u32();
    e.pos = getVec3(r); e.orientation = r.f32(); e.moving = r.u8() != 0; e.speed = r.f32();
    e.name = getStr(r);
    return e;
}
EntityRemove decodeEntityRemove(const std::vector<uint8_t>& p) {
    ByteReader r(p); EntityRemove e; e.guid = r.u64(); return e;
}

// ---- apply into a DebugDraw ----
void apply(DebugDraw& dd, const DebugMarker& m) {
    DebugCategory cat = categoryFor(m.type);
    dd.cross(m.pos, 2.0f, m.color, cat);
    dd.point(m.pos, m.color, cat);
}
void apply(DebugDraw& dd, const DebugLine& l) {
    DebugCategory cat = categoryFor(l.type);
    dd.line(l.from, l.to, l.color, cat);
    if (l.hasHit) dd.point(l.hit, l.color, DebugCategory::HitPoint);
}
void apply(DebugDraw& dd, const DebugPath& d) {
    dd.path(d.points, d.color, DebugCategory::NavPath, /*markers*/true);
}
void apply(DebugDraw& dd, const DebugVolume& v) {
    DebugCategory cat = categoryFor(v.type);
    if (v.kind == 1) {
        dd.sphere(v.center, v.radius, v.color, cat);
    } else {
        Vec3 mn{ v.center.x - v.half.x, v.center.y - v.half.y, v.center.z - v.half.z };
        Vec3 mx{ v.center.x + v.half.x, v.center.y + v.half.y, v.center.z + v.half.z };
        dd.aabb(mn, mx, v.color, cat);
    }
}

} // namespace wf
