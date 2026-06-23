#include "fxbridge.hpp"
#include "byte_reader.hpp"
#include "byte_writer.hpp"

namespace wf {

namespace {
void putTarget(ByteWriter& w, const FxTarget& t) {
    w.u8(static_cast<uint8_t>(t.scope)); w.u64(t.guid); w.u32(t.zoneId);
}
FxTarget getTarget(ByteReader& r) {
    FxTarget t; t.scope = static_cast<FxScope>(r.u8()); t.guid = r.u64(); t.zoneId = r.u32();
    return t;
}
void putStr(ByteWriter& w, const std::string& s) {
    uint16_t n = static_cast<uint16_t>(s.size() > 0xFFFF ? 0xFFFF : s.size());
    w.u16(n); w.bytes(reinterpret_cast<const uint8_t*>(s.data()), n);
}
std::string getStr(ByteReader& r) {
    uint16_t n = r.u16(); std::string s(reinterpret_cast<const char*>(r.ptr()), n); r.skip(n); return s;
}
} // namespace

// ---- encode ----
std::vector<uint8_t> encode(const WeatherFx& o) {
    ByteWriter w; w.u32(static_cast<uint32_t>(o.type)); w.f32(o.grade); w.u32(o.soundId);
    w.u8(o.instant ? 1 : 0); putTarget(w, o.target); w.u32(o.opId);
    return frame(EDITOR_FX_WEATHER, w.data());
}
std::vector<uint8_t> encode(const SoundFx& o) {
    ByteWriter w; w.u32(o.soundId); w.u8(o.music ? 1 : 0); putTarget(w, o.target); w.u32(o.opId);
    return frame(EDITOR_FX_SOUND, w.data());
}
std::vector<uint8_t> encode(const CinematicFx& o) {
    ByteWriter w; w.u32(o.cinematicId); putTarget(w, o.target); w.u32(o.opId);
    return frame(EDITOR_FX_CINEMATIC, w.data());
}
std::vector<uint8_t> encode(const WorldStateFx& o) {
    ByteWriter w; w.u32(o.field); w.u32(o.value); putTarget(w, o.target); w.u32(o.opId);
    return frame(EDITOR_FX_WORLDSTATE, w.data());
}
std::vector<uint8_t> encode(const ScreenMsgFx& o) {
    ByteWriter w; w.u8(o.kind); w.u32(o.type); putStr(w, o.text); putTarget(w, o.target); w.u32(o.opId);
    return frame(EDITOR_FX_SCREENMSG, w.data());
}
std::vector<uint8_t> encode(const TimeSpeedFx& o) {
    ByteWriter w; w.u32(o.packedDate); w.f32(o.speed); w.u32(o.opId);
    return frame(EDITOR_FX_TIMESPEED, w.data());
}
std::vector<uint8_t> encode(const ZoneAttackFx& o) {
    ByteWriter w; w.u32(o.zoneId); w.u32(o.opId);
    return frame(EDITOR_FX_ZONEATTACK, w.data());
}

// ---- decode ----
WeatherFx decodeWeatherFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); WeatherFx o;
    o.type = static_cast<WeatherType>(r.u32()); o.grade = r.f32(); o.soundId = r.u32();
    o.instant = r.u8() != 0; o.target = getTarget(r); o.opId = r.u32();
    return o;
}
SoundFx decodeSoundFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); SoundFx o;
    o.soundId = r.u32(); o.music = r.u8() != 0; o.target = getTarget(r); o.opId = r.u32();
    return o;
}
CinematicFx decodeCinematicFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); CinematicFx o;
    o.cinematicId = r.u32(); o.target = getTarget(r); o.opId = r.u32();
    return o;
}
WorldStateFx decodeWorldStateFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); WorldStateFx o;
    o.field = r.u32(); o.value = r.u32(); o.target = getTarget(r); o.opId = r.u32();
    return o;
}
ScreenMsgFx decodeScreenMsgFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); ScreenMsgFx o;
    o.kind = r.u8(); o.type = r.u32(); o.text = getStr(r); o.target = getTarget(r); o.opId = r.u32();
    return o;
}
TimeSpeedFx decodeTimeSpeedFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); TimeSpeedFx o;
    o.packedDate = r.u32(); o.speed = r.f32(); o.opId = r.u32();
    return o;
}
ZoneAttackFx decodeZoneAttackFx(const std::vector<uint8_t>& p) {
    ByteReader r(p); ZoneAttackFx o; o.zoneId = r.u32(); o.opId = r.u32(); return o;
}

// ---- realise: editor op -> the SMSG packet the server broadcasts ----
std::vector<uint8_t> realise(const WeatherFx& o) {
    return buildWeather(o.type, o.grade, o.soundId, o.instant);
}
std::vector<uint8_t> realise(const SoundFx& o) {
    return o.music ? buildPlayMusic(o.soundId) : buildPlaySound(o.soundId);
}
std::vector<uint8_t> realise(const CinematicFx& o) {
    return buildTriggerCinematic(o.cinematicId);
}
std::vector<uint8_t> realise(const WorldStateFx& o) {
    return buildUpdateWorldState(o.field, o.value);
}
std::vector<uint8_t> realise(const ScreenMsgFx& o) {
    if (o.kind == 1) return buildNotification(o.text);
    if (o.kind == 2) return buildServerMessage(o.type, o.text);
    return buildAreaTriggerMessage(o.text);                 // kind 0 (default)
}
std::vector<uint8_t> realise(const TimeSpeedFx& o) {
    return buildLoginSetTimeSpeed(o.packedDate, o.speed);
}
std::vector<uint8_t> realise(const ZoneAttackFx& o) {
    return buildZoneUnderAttack(o.zoneId);
}

} // namespace wf
