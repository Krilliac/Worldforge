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

## SMSG_OVERRIDE_LIGHT does not exist in vanilla

The `live-override-commands-plan` names `SMSG_OVERRIDE_LIGHT` as its primary new
primitive. **It is not a valid 1.12.1 opcode.** mangos-zero's `Opcodes.h` is a
multi-expansion table; the `[-ZERO] Last existed in 1.12.1 opcode` marker sits
around `0x342`, and `SMSG_OVERRIDE_LIGHT = 0x411` is above it (a TBC+/placeholder
entry). cmangos's vanilla-only enum ends at `SMSG_DEFENSE_MESSAGE = 0x33B` and
contains no override-light at all. **A 1.12.1 (5875) client has no handler for
0x411 — sending it does nothing (or desyncs).**

Implication for the `.light` command: on vanilla there is no per-player
"override light" packet. Atmospheric lighting changes must instead go through
content the 1.12 client *does* react to — e.g. weather (`SMSG_WEATHER`), zone
light via DBC/`Light.dbc` area definitions (static, not per-player), or time of
day. The other override commands (`.weather/.music/.sound/.cinematic/.worldstate/
.zoneattack/.screenmsg/.timespeed`) all map onto opcodes that **do** exist above
and are covered by `clientfx`.

## How it plugs in

- **Server side:** wrap each builder's layout in the existing `WorldPacket` +
  `SendPacket` pattern; register `.light/.weather/...` as chat handlers (the
  live-override plan). `clientfx` is the layout reference.
- **WorldForge engine:** call a builder, hand the bytes to
  `WorldHeaderCrypt::encryptSend`, and write to the socket — the editor's future
  Atmosphere/World panel drives these directly for a connected client, or routes
  them through the editor bridge to the server (`EDITOR_RESEARCH.md` D.4).
