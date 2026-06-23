#include "editor_bridge.hpp"
#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "worldproto.hpp"   // reuse the client-header framing convention

namespace wf {

namespace {
void putVec3(ByteWriter& w, const Vec3& v) { w.f32(v.x); w.f32(v.y); w.f32(v.z); }
Vec3 getVec3(ByteReader& r) { float x = r.f32(), y = r.f32(), z = r.f32(); return {x, y, z}; }
} // namespace

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

} // namespace wf
