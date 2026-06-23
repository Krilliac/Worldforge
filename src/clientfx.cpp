#include "clientfx.hpp"
#include "byte_writer.hpp"
#include "worldproto.hpp"

namespace wf {

namespace {
// Header (size BE + opcode LE) + body -> a complete server packet.
std::vector<uint8_t> framed(uint16_t opcode, const ByteWriter& body) {
    std::vector<uint8_t> pkt = writeServerHeader(opcode, static_cast<uint32_t>(body.size()));
    const std::vector<uint8_t>& b = body.data();
    pkt.insert(pkt.end(), b.begin(), b.end());
    return pkt;
}
// Write a C-string: the bytes followed by a NUL terminator (mangos `<< szStr`).
void putCStr(ByteWriter& w, const std::string& s) {
    w.bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    w.u8(0);
}
} // namespace

// --- audio ---------------------------------------------------------------
std::vector<uint8_t> buildPlaySound(uint32_t soundId) {
    ByteWriter w; w.u32(soundId);
    return framed(SMSG_PLAY_SOUND, w);
}
std::vector<uint8_t> buildPlayMusic(uint32_t soundId) {
    ByteWriter w; w.u32(soundId);
    return framed(SMSG_PLAY_MUSIC, w);
}
std::vector<uint8_t> buildPlayObjectSound(uint32_t soundId, uint64_t guid) {
    ByteWriter w; w.u32(soundId); w.u64(guid);   // id first, then guid
    return framed(SMSG_PLAY_OBJECT_SOUND, w);
}

// --- spectacle -----------------------------------------------------------
std::vector<uint8_t> buildTriggerCinematic(uint32_t cinematicSequenceId) {
    ByteWriter w; w.u32(cinematicSequenceId);
    return framed(SMSG_TRIGGER_CINEMATIC, w);
}
std::vector<uint8_t> buildPlaySpellVisual(uint64_t guid, uint32_t kitId) {
    ByteWriter w; w.u64(guid); w.u32(kitId);
    return framed(SMSG_PLAY_SPELL_VISUAL, w);
}
std::vector<uint8_t> buildPlaySpellImpact(uint64_t guid, uint32_t kitId) {
    ByteWriter w; w.u64(guid); w.u32(kitId);
    return framed(SMSG_PLAY_SPELL_IMPACT, w);
}
std::vector<uint8_t> buildGameObjectCustomAnim(uint64_t guid, uint32_t anim) {
    ByteWriter w; w.u64(guid); w.u32(anim);
    return framed(SMSG_GAMEOBJECT_CUSTOM_ANIM, w);
}

// --- HUD / text ----------------------------------------------------------
std::vector<uint8_t> buildAreaTriggerMessage(const std::string& text) {
    ByteWriter w;
    w.u32(static_cast<uint32_t>(text.size() + 1));   // length includes the NUL
    putCStr(w, text);
    return framed(SMSG_AREA_TRIGGER_MESSAGE, w);
}
std::vector<uint8_t> buildNotification(const std::string& text) {
    ByteWriter w; putCStr(w, text);                  // string only, no length
    return framed(SMSG_NOTIFICATION, w);
}
std::vector<uint8_t> buildServerMessage(uint32_t type, const std::string& text) {
    ByteWriter w; w.u32(type); putCStr(w, text);
    return framed(SMSG_SERVER_MESSAGE, w);
}
std::vector<uint8_t> buildMinimapPing(uint64_t guid, float x, float y) {
    ByteWriter w; w.u64(guid); w.f32(x); w.f32(y);
    return framed(MSG_MINIMAP_PING, w);
}

// --- world state / atmosphere -------------------------------------------
std::vector<uint8_t> buildUpdateWorldState(uint32_t field, uint32_t value) {
    ByteWriter w; w.u32(field); w.u32(value);
    return framed(SMSG_UPDATE_WORLD_STATE, w);
}
std::vector<uint8_t> buildInitWorldStates(uint32_t mapId, uint32_t zoneId,
        const std::vector<std::pair<uint32_t, uint32_t>>& states) {
    ByteWriter w;
    w.u32(mapId);
    w.u32(zoneId);                                   // vanilla: no areaId field
    w.u16(static_cast<uint16_t>(states.size()));     // count of field/value blocks
    for (const auto& s : states) { w.u32(s.first); w.u32(s.second); }
    return framed(SMSG_INIT_WORLD_STATES, w);
}
std::vector<uint8_t> buildWeather(WeatherType type, float grade,
        uint32_t soundId, bool instant) {
    ByteWriter w;
    w.u32(static_cast<uint32_t>(type));
    w.f32(grade);
    w.u32(soundId);                                  // vanilla-only field
    w.u8(instant ? 1 : 0);                           // 1 = instant, 0 = smooth
    return framed(SMSG_WEATHER, w);
}
std::vector<uint8_t> buildZoneUnderAttack(uint32_t zoneId) {
    ByteWriter w; w.u32(zoneId);
    return framed(SMSG_ZONE_UNDER_ATTACK, w);
}
std::vector<uint8_t> buildLoginSetTimeSpeed(uint32_t packedDate, float gameSpeed) {
    ByteWriter w; w.u32(packedDate); w.f32(gameSpeed);
    return framed(SMSG_LOGIN_SETTIMESPEED, w);
}

// --- creatures / objects -------------------------------------------------
std::vector<uint8_t> buildEmote(uint64_t guid, uint32_t emoteId) {
    ByteWriter w; w.u32(emoteId); w.u64(guid);    // emote id first, then guid
    return framed(SMSG_EMOTE, w);
}
std::vector<uint8_t> buildAiReaction(uint64_t guid, AiReaction reaction) {
    ByteWriter w; w.u64(guid); w.u32(static_cast<uint32_t>(reaction));
    return framed(SMSG_AI_REACTION, w);
}
std::vector<uint8_t> buildExplorationExperience(uint32_t areaId, uint32_t xp) {
    ByteWriter w; w.u32(areaId); w.u32(xp);
    return framed(SMSG_EXPLORATION_EXPERIENCE, w);
}
std::vector<uint8_t> buildGameObjectDespawnAnim(uint64_t guid) {
    ByteWriter w; w.u64(guid);
    return framed(SMSG_GAMEOBJECT_DESPAWN_ANIM, w);
}
std::vector<uint8_t> buildStandState(uint8_t state) {
    ByteWriter w; w.u8(state);
    return framed(SMSG_STANDSTATE_UPDATE, w);
}

std::vector<uint8_t> buildOverrideLight(uint32_t currentZoneLightId,
        uint32_t overrideLightId, uint32_t fadeInMs) {
    ByteWriter w;
    w.u32(currentZoneLightId);   // fade FROM (zone default / current light id)
    w.u32(overrideLightId);      // fade TO
    w.u32(fadeInMs);
    return framed(SMSG_OVERRIDE_LIGHT, w);
}

uint32_t packTimeBitFields(int year, int month, int mday, int wday, int hour, int minute) {
    // mangos uses tm_year (years since 1900): (tm_year - 100) == (fullYear - 2000).
    return (static_cast<uint32_t>(year - 2000) << 24)
         | (static_cast<uint32_t>(month)      << 20)
         | (static_cast<uint32_t>(mday - 1)   << 14)
         | (static_cast<uint32_t>(wday)       << 11)
         | (static_cast<uint32_t>(hour)       <<  6)
         |  static_cast<uint32_t>(minute);
}

} // namespace wf
