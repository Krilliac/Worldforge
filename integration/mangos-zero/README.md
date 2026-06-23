# WorldForge ↔ mangos-zero bridge module

A drop-in server-side module that receives WorldForge editor RPCs over a private
TCP port and applies them to the live world, so the editor can move/spawn
objects, push waypoints, drive atmosphere FX (weather/sound/cinematic/world-
state/screen-message/time/zone-attack/override-light), and stream debug data —
all reaching unmodified 1.12.1 clients through mangos's existing broadcast path.

This is a **sketch / starting point**, not a compilable WorldForge target: it
`#include`s the WorldForge wire headers (which are byte-exact and unit-tested)
*and* mangos-zero headers, so it only builds inside your server tree. The
WorldForge-side calls (`wf::decode*`, `wf::realise`, `wf::build*`) are real; the
mangos calls use the actual class/method names from the current tree.

## What it does

```
[editor TCP] --frames--> acceptor thread --queue--> World::Update drains
   --> decode EDITOR_* --> {object edit | FX realise->SMSG broadcast | debug}
   --> mangos mutates state / sends packets --> in-range clients react
```

- **Object edits** (`EDITOR_MOVE_OBJECT/SPAWN_CREATURE/DESPAWN/SET_WAYPOINTS`):
  call `Map::CreatureRelocation`, `WorldObject::SummonCreature`,
  `Creature::ForcedDespawn`, `MotionMaster::MoveWaypoint` — the same authoritative
  setters scripts use, so visibility/broadcast bookkeeping stays correct.
- **Atmosphere FX** (`EDITOR_FX_*`, `EDITOR_OVERRIDE_LIGHT`): `wf::realise()`
  produces the verified SMSG bytes; the module rebuilds a `WorldPacket` and
  broadcasts it to the op's `FxTarget` scope (self / target / zone / server).
- **Debug stream** (`EDITOR_DEBUG_*`): optional — relay the server's `.debug vis`
  captures down the same socket for the WorldForge viewport (see PR #386).

## Integrating

1. Copy `WorldForgeBridge.{h,cpp}` into `src/game/WorldForge/` and add the dir to
   `src/game/CMakeLists.txt`. Vendor the WorldForge `src/*.hpp` wire headers
   (`editor_bridge.hpp`, `fxbridge.hpp`, `clientfx.hpp`, `byte_*.hpp`,
   `worldproto.hpp`, `math.hpp`, `image.hpp`) or add them as an include path.
2. In `World::SetInitialWorldSettings()` (or wherever services start), call
   `WorldForgeBridge::instance().Start(<port>)` behind a config flag.
3. In `World::Update(uint32 diff)`, next to `ProcessCliCommands()`, call
   `WorldForgeBridge::instance().Process()`. (Global/FX ops are safe on the world
   thread. Map-scoped object edits should be posted into the owning `Map`'s
   update — see the `// THREADING` note in the .cpp.)
4. Gate the listener to admin access (mirror `RASocket`'s `RA.MinLevel` auth) and
   bind to localhost; this link is trusted and unceremoniously powerful.

## Editor side

The editor connects with `wf::editor::BridgeClient` (`src/editor/BridgeClient.*`):
it ships the panels' framed `EDITOR_*` packets to this module's port and folds
the server's `EDITOR_DEBUG_*` stream back into the viewport overlay. Bind this
module to the same host/port (default `127.0.0.1:7878`).

## Try it without mangos first

`wforge-stub-server` (built from `src/server/`) is the same protocol backed by an
in-memory `WorldSim` instead of mangos -- run it, point the editor at it, and the
full round-trip (ops in, world mutated, acks + debug streamed back) works with no
server tree. The dispatch here mirrors that stub's; swap `WorldSim` calls for the
mangos calls below.

## Security

The bridge is an authenticated local control channel, **not** part of the game
opcode stream — game clients cannot reach it. Bind to `127.0.0.1`, require
`SEC_ADMINISTRATOR`, and never expose the port publicly.
