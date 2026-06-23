#pragma once
// ---------------------------------------------------------------------------
// fxbridge: the Atmosphere / World "client-FX" override surface. These are the
// editor -> server RPCs behind the live-override commands (weather, music/sound,
// cinematic, world-state, screen messages, time speed, zone-under-attack). Each
// carries an FxTarget (scope + guid/zone) the raw SMSG body has no room for.
//
// Round trip:  editor encode(op) --bridge--> server decode + realise(op) -->
// the verified clientfx SMSG packet --> broadcast to the scoped recipients.
//
// realise() lives here (reusing clientfx) so it is unit-tested in-tree and is
// the exact reference the server module copies. Editor-side encode/decode and
// the framing come from editor_bridge.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "editor_bridge.hpp"   // FxScope, FxTarget, EditorOpcode, frame/readFrame
#include "clientfx.hpp"        // WeatherType + the SMSG builders

namespace wf {

struct WeatherFx     { WeatherType type = WeatherType::Fine; float grade = 0.0f; uint32_t soundId = 0; bool instant = false; FxTarget target; uint32_t opId = 0; };
struct SoundFx       { uint32_t soundId = 0; bool music = false; FxTarget target; uint32_t opId = 0; };   // music ? PLAY_MUSIC : PLAY_SOUND
struct CinematicFx   { uint32_t cinematicId = 0; FxTarget target; uint32_t opId = 0; };
struct WorldStateFx  { uint32_t field = 0; uint32_t value = 0; FxTarget target; uint32_t opId = 0; };
struct ScreenMsgFx   { uint8_t kind = 0; uint32_t type = 0; std::string text; FxTarget target; uint32_t opId = 0; }; // kind: 0 area-trigger, 1 notification, 2 server msg
struct TimeSpeedFx   { uint32_t packedDate = 0; float speed = 0.01666667f; uint32_t opId = 0; };          // server-wide
struct ZoneAttackFx  { uint32_t zoneId = 0; uint32_t opId = 0; };

// ---- editor-side encode (-> framed editor packet) ----
std::vector<uint8_t> encode(const WeatherFx&);
std::vector<uint8_t> encode(const SoundFx&);
std::vector<uint8_t> encode(const CinematicFx&);
std::vector<uint8_t> encode(const WorldStateFx&);
std::vector<uint8_t> encode(const ScreenMsgFx&);
std::vector<uint8_t> encode(const TimeSpeedFx&);
std::vector<uint8_t> encode(const ZoneAttackFx&);

// ---- server-side decode (from a frame payload) ----
WeatherFx    decodeWeatherFx(const std::vector<uint8_t>&);
SoundFx      decodeSoundFx(const std::vector<uint8_t>&);
CinematicFx  decodeCinematicFx(const std::vector<uint8_t>&);
WorldStateFx decodeWorldStateFx(const std::vector<uint8_t>&);
ScreenMsgFx  decodeScreenMsgFx(const std::vector<uint8_t>&);
TimeSpeedFx  decodeTimeSpeedFx(const std::vector<uint8_t>&);
ZoneAttackFx decodeZoneAttackFx(const std::vector<uint8_t>&);

// ---- realise an op into the SMSG packet the server broadcasts to its scope ----
std::vector<uint8_t> realise(const WeatherFx&);
std::vector<uint8_t> realise(const SoundFx&);
std::vector<uint8_t> realise(const CinematicFx&);
std::vector<uint8_t> realise(const WorldStateFx&);
std::vector<uint8_t> realise(const ScreenMsgFx&);
std::vector<uint8_t> realise(const TimeSpeedFx&);
std::vector<uint8_t> realise(const ZoneAttackFx&);

} // namespace wf
