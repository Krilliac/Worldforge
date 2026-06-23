#pragma once
// ---------------------------------------------------------------------------
// clientfx: builders for vanilla 1.12.1 (build 5875) "the client renders it for
// free" server opcodes -- sound, music, cinematics, area-trigger text,
// notifications, minimap pings, world-state, weather, spell visuals, custom GO
// anims. The 1.12 client has handlers for all of these; mangos-zero leaves
// several effectively unused (debug-only), so they are easy to drive from a
// tool. Each builder returns a COMPLETE framed server packet (4-byte header +
// body) ready for WorldHeaderCrypt::encryptSend.
//
// Byte layouts are verified against mangos-zero and cmangos handler source; the
// round-trip is unit-tested. This is the shared reference for the server-side
// `.light/.weather/.music/...` override commands AND lets the WorldForge engine
// build/send these packets directly over its worldproto link.
//
// SMSG_OVERRIDE_LIGHT (0x411) is a special case: it is TBC+, so it is NOT a
// render path for a 1.12.1 RETAIL client -- but it IS in mangos-zero's opcode
// table, so the trusted WorldForge<->server link can use it as a server-handled
// CUSTOM message (the server applies the light and translates it to something a
// vanilla client can see). buildOverrideLight produces that packet; see also
// the editor-bridge EDITOR_OVERRIDE_LIGHT op for a scope-aware alternative.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wf {

// Vanilla weather states (SMSG_WEATHER `type`).
enum class WeatherType : uint32_t { Fine = 0, Rain = 1, Snow = 2, Sandstorm = 3 };

// SMSG_AI_REACTION `reaction` values.
enum class AiReaction : uint32_t { Alert = 0, Friendly = 1, Hostile = 2, Destroy = 3 };

// --- audio ---------------------------------------------------------------
std::vector<uint8_t> buildPlaySound(uint32_t soundId);                       // SMSG_PLAY_SOUND
std::vector<uint8_t> buildPlayMusic(uint32_t soundId);                       // SMSG_PLAY_MUSIC
std::vector<uint8_t> buildPlayObjectSound(uint32_t soundId, uint64_t guid);  // SMSG_PLAY_OBJECT_SOUND (id THEN guid)

// --- spectacle -----------------------------------------------------------
std::vector<uint8_t> buildTriggerCinematic(uint32_t cinematicSequenceId);   // SMSG_TRIGGER_CINEMATIC
std::vector<uint8_t> buildPlaySpellVisual(uint64_t guid, uint32_t kitId);    // SMSG_PLAY_SPELL_VISUAL
std::vector<uint8_t> buildPlaySpellImpact(uint64_t guid, uint32_t kitId);    // SMSG_PLAY_SPELL_IMPACT
std::vector<uint8_t> buildGameObjectCustomAnim(uint64_t guid, uint32_t anim);// SMSG_GAMEOBJECT_CUSTOM_ANIM

// --- HUD / text ----------------------------------------------------------
std::vector<uint8_t> buildAreaTriggerMessage(const std::string& text);      // SMSG_AREA_TRIGGER_MESSAGE (uint32 len + cstr)
std::vector<uint8_t> buildNotification(const std::string& text);            // SMSG_NOTIFICATION (cstr only)
std::vector<uint8_t> buildServerMessage(uint32_t type, const std::string& text); // SMSG_SERVER_MESSAGE
std::vector<uint8_t> buildMinimapPing(uint64_t guid, float x, float y);     // MSG_MINIMAP_PING

// --- world state / atmosphere -------------------------------------------
std::vector<uint8_t> buildUpdateWorldState(uint32_t field, uint32_t value); // SMSG_UPDATE_WORLD_STATE
std::vector<uint8_t> buildInitWorldStates(uint32_t mapId, uint32_t zoneId,
        const std::vector<std::pair<uint32_t, uint32_t>>& states);          // SMSG_INIT_WORLD_STATES (no areaId in vanilla)
std::vector<uint8_t> buildWeather(WeatherType type, float grade,
        uint32_t soundId, bool instant);                                    // SMSG_WEATHER (vanilla has the soundId field)
std::vector<uint8_t> buildZoneUnderAttack(uint32_t zoneId);                 // SMSG_ZONE_UNDER_ATTACK
std::vector<uint8_t> buildLoginSetTimeSpeed(uint32_t packedDate,
        float gameSpeed = 0.01666667f);                                     // SMSG_LOGIN_SETTIMESPEED

// --- creatures / objects (spectacle + debug) ----------------------------
std::vector<uint8_t> buildEmote(uint64_t guid, uint32_t emoteId);           // SMSG_EMOTE (u32 emote THEN guid)
std::vector<uint8_t> buildAiReaction(uint64_t guid, AiReaction reaction);   // SMSG_AI_REACTION (aggro blip)
std::vector<uint8_t> buildExplorationExperience(uint32_t areaId, uint32_t xp); // SMSG_EXPLORATION_EXPERIENCE
std::vector<uint8_t> buildGameObjectDespawnAnim(uint64_t guid);             // SMSG_GAMEOBJECT_DESPAWN_ANIM
std::vector<uint8_t> buildStandState(uint8_t state);                        // SMSG_STANDSTATE_UPDATE

// SMSG_OVERRIDE_LIGHT (0x411). Custom / trusted-link only (see header note):
// the TBC/WotLK body is three uint32 -- the light to fade FROM (the zone's
// current/default light id), the override light to fade TO, and the fade time
// in ms. NOT for a vanilla retail client; for the WorldForge<->server bridge.
std::vector<uint8_t> buildOverrideLight(uint32_t currentZoneLightId,
        uint32_t overrideLightId, uint32_t fadeInMs);

// Bit-pack a calendar time the way the client expects (mangos secsToTimeBitFields,
// where the year field is tm_year-100 == fullYear-2000):
//   ((year-2000)<<24)|(month<<20)|((mday-1)<<14)|(wday<<11)|(hour<<6)|minute
// year is the full year (e.g. 2026), month 0..11, mday 1..31, wday 0..6.
uint32_t packTimeBitFields(int year, int month, int mday, int wday, int hour, int minute);

} // namespace wf
