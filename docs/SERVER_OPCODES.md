# WorldForge — Vanilla 1.12.1 Server-FX Opcodes

A set of vanilla **1.12.1 (build 5875)** server opcodes that the retail client
already knows how to render but mangos-zero leaves **unused or debug-only**.
WorldForge implements packet **builders** for them (`clientfx.{hpp,cpp}`) so the
same byte-exact layout is available to (a) the server-side `.light/.weather/...`
override commands and (b) the WorldForge engine, which can build and send these
straight over its `worldproto` link (framing + header cipher already in tree).

> All values + layouts verified against **mangoszero/server** and
> **cmangos/mangos-classic** `src/game/Server/Opcodes.h` and the handler source
> (build 5875). Bodies are little-endian; `<< szStr` writes a NUL-terminated
> C-string.

## Opcode table

| Opcode | Value | Body layout | mangos-zero status |
|--------|------:|-------------|--------------------|
| `SMSG_PLAY_SOUND` | `0x2D2` | `u32 soundId` | used (sound system) |
| `SMSG_PLAY_MUSIC` | `0x277` | `u32 soundId` | used |
| `SMSG_PLAY_OBJECT_SOUND` | `0x278` | `u32 soundId; u64 guid` | used |
| `SMSG_TRIGGER_CINEMATIC` | `0x0FA` | `u32 cinematicSequenceId` | used (intros) |
| `SMSG_PLAY_SPELL_VISUAL` | `0x1F3` | `u64 guid; u32 kitId` | used (trainers) |
| `SMSG_PLAY_SPELL_IMPACT` | `0x1F7` | `u64 guid; u32 kitId` | **rare / debug** |
| `SMSG_GAMEOBJECT_CUSTOM_ANIM` | `0x0B3` | `u64 guid; u32 anim` | **underused** |
| `SMSG_AREA_TRIGGER_MESSAGE` | `0x2B8` | `u32 len(=strlen+1); cstr` | used |
| `SMSG_NOTIFICATION` | `0x1CB` | `cstr` (no length) | used |
| `SMSG_SERVER_MESSAGE` | `0x291` | `u32 type; cstr` | used (shutdown) |
| `MSG_MINIMAP_PING` | `0x1D5` | `u64 guid; f32 x; f32 y` | **relay only — no server API** |
| `SMSG_UPDATE_WORLD_STATE` | `0x2C3` | `u32 field; u32 value` | used (BG/PvP) |
| `SMSG_INIT_WORLD_STATES` | `0x2C2` | `u32 map; u32 zone; u16 count; count×{u32,u32}` | used |
| `SMSG_WEATHER` | `0x2F4` | `u32 type; f32 grade; u32 soundId; u8 instant` | used |
| `SMSG_ZONE_UNDER_ATTACK` | `0x254` | `u32 zoneId` | used (invasions) |
| `SMSG_LOGIN_SETTIMESPEED` | `0x042` | `u32 packedDate; f32 speed` | used (every login) |

The **debug/underused** rows (PLAY_SPELL_IMPACT, GAMEOBJECT_CUSTOM_ANIM,
server-initiated MINIMAP_PING) are the strongest "newly implement" candidates:
the client handles them but the core almost never sends them.

## Vanilla-specific quirks (easy to get wrong)

- **`SMSG_WEATHER` carries an extra `u32 soundId`** between `grade` and the
  transition byte in 1.12 — later cores drop it. `buildWeather` includes it.
- **`SMSG_INIT_WORLD_STATES` has no `areaId`** in vanilla — TBC+ inserts a
  `u32 areaId` after `zoneId`. `buildInitWorldStates` omits it.
- **`SMSG_PLAY_OBJECT_SOUND` is id-then-guid** (sound id first), not the reverse.
- **`SMSG_AREA_TRIGGER_MESSAGE`** has a `u32` length prefix (incl. the NUL);
  **`SMSG_NOTIFICATION`** is a bare C-string with no length.
- **`SMSG_LOGIN_SETTIMESPEED`** packs the date as mangos `secsToTimeBitFields`:
  `((year-2000)<<24)|(mon<<20)|((mday-1)<<14)|(wday<<11)|(hour<<6)|min`, speed
  fixed at `1/60`. See `packTimeBitFields`.

## SMSG_OVERRIDE_LIGHT — not a vanilla *client* opcode, but a usable custom one

`SMSG_OVERRIDE_LIGHT = 0x411` is **TBC+**: it is above mangos-zero's
`[-ZERO] Last existed in 1.12.1 opcode` marker (~`0x342`), and cmangos's
vanilla-only enum ends at `SMSG_DEFENSE_MESSAGE = 0x33B` with no override-light
at all. So a **1.12.1 retail client has no handler** — you cannot send `0x411`
*to a vanilla client* and have it render.

**But the opcode number is in mangos-zero's table, and the WorldForge↔server
link is a trusted channel we own both ends of.** So `0x411` is perfectly usable
as a **server-handled custom message**: WorldForge frames it, the server's
handler receives it and acts. The server is then the *translator* — it applies
the light and produces whatever a vanilla client can actually see (or uses it for
the editor's own viewport preview). This is the user's design: a custom opcode we
manipulate because it already exists server-side.

WorldForge provides two ways to drive it (pick whichever the server listens on):

1. **`clientfx::buildOverrideLight(currentZoneLightId, overrideLightId, fadeInMs)`**
   — frames the literal `0x411` packet (TBC body: three `uint32` — fade-from,
   fade-to, fade-ms). Use if the server keys on the raw opcode number.
2. **`editor_bridge` `EDITOR_OVERRIDE_LIGHT` (0x4006)** — a scope-aware editor
   RPC: `{ overrideLightId, fadeInMs, scope(self/target/zone/server), targetGuid,
   zoneId, opId }`. Carries targeting the raw SMSG body has no room for, and
   rides the existing editor channel.

What the server does with it on vanilla (translation options): drive `SMSG_WEATHER`
for a mood shift, swap the zone's `Light.dbc` association, change time-of-day, or
just feed WorldForge's viewport light. The point is the *id + fade + scope*
travels cleanly from the editor to the server; the server owns the vanilla-safe
realisation.

The other override commands (`.weather/.music/.sound/.cinematic/.worldstate/
.zoneattack/.screenmsg/.timespeed`) map onto opcodes that exist for vanilla and
are covered directly by `clientfx`.

## Editor FX bridge (`fxbridge`)

The editor doesn't usually send raw SMSG; it sends a **scope-aware request** and
the server realises it. `fxbridge` defines those `EDITOR_FX_*` ops — `WeatherFx`,
`SoundFx`, `CinematicFx`, `WorldStateFx`, `ScreenMsgFx`, `TimeSpeedFx`,
`ZoneAttackFx` (plus `OverrideLight`) — each carrying an `FxTarget {
scope(self/target/zone/server), guid, zoneId }`. `wf::realise(op)` maps each to
the verified `clientfx` SMSG, so the editor→server→packet path is unit-tested
in-tree and is the exact reference the server copies.

```
editor encode(WeatherFx{Snow, scope=Zone, zone=1519})
   --bridge--> server decodeWeatherFx --> realise() == buildWeather(...)
   --> broadcast SMSG_WEATHER to every player in zone 1519
```

## How it plugs in

- **Server side:** drop in `integration/mangos-zero/WorldForgeBridge.{h,cpp}` —
  it drains frames at the `World::Update` tick, decodes `EDITOR_*`, calls
  `wf::realise()` for FX (or the authoritative `Map::CreatureRelocation` /
  `SummonCreature` / `ForcedDespawn` / `MotionMaster` for object edits), and
  broadcasts to the op's scope. `clientfx`/`fxbridge` are the byte-exact
  reference; the `.light/.weather/...` chat commands can share the same realise().
- **WorldForge engine:** call a builder (or `realise`), hand the bytes to
  `WorldHeaderCrypt::encryptSend`, and write to the socket — the editor's
  Atmosphere/World panel drives these via the bridge (`EDITOR_RESEARCH.md` D.4).
