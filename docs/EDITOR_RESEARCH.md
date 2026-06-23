# WorldForge — Editor Research & Design Synthesis

Consolidated reference for building the **WorldForge editor**: a 3D world-authoring
tool for WoW **1.12.1 (vanilla)** content that mutates a running **mangos-zero**
server so unmodified retail clients see edits live.

It folds together four research strands:

1. **Spark Engine** (`github.com/Krilliac/SparkEngine`) — an existing C++23 engine
   with a mature ImGui editor; the concrete *look* and a directly reusable
   architecture (gizmos, terrain tooling, live-edit bridge).
2. **Noggit / Noggit Red** — the reference WoW map editor; the WoW-specific UX
   (per-chunk tools, ≤4-layer texture splat, tileset chooser).
3. **WoW 1.12.1 client reverse-engineering** (OwnedCore, wowdev.wiki, cMaNGOS) —
   coordinate math, camera/world-to-screen, the object model.
4. **mangos-zero / cMaNGOS server internals** — how to mutate live world state on
   the right thread so the existing broadcast path reaches clients.

> Provenance: all findings are from public format/RE documentation and clean-room
> reading of open-source emulator/editor code. No Blizzard code or assets.

---

## Part A — The editor look (visual spec)

The target look is the **Spark Engine editor**: a dark, dockable ImGui workspace.
WorldForge reskins/repurposes it for WoW content rather than inventing a new UI.

### A.1 Spark's actual chrome (from `docs/screenshots/editor-overview.png`)

- **Theme:** near-black background (~`#1A1D21`), single **teal/cyan accent**
  (~`#2DD4BF`) for active toggles, selection, and the logo. Light-grey text.
  Alternate themes ship (blue-accent, high-contrast, JetBrains) via `EditorTheme`.
- **Top menu bar:** `File · Edit · GameObject · Window · FPS Tools · Build · Help`
  (WorldForge swaps the genre menu, e.g. `World · Terrain · Objects · Server`).
- **Play-mode toolbar:** ▶ Play / ❚❚ Pause / ■ Stop, plus a **Snap** toggle.
- **Right dock — Inspector:** "Select an object to inspect" empty state; renders
  per-component property rows when something is selected.
- **Bottom dock — Asset Browser:** `Import · Refresh · <thumbnail-size px> · Assets`
  breadcrumb, a folder tree on the left, a thumbnail grid on the right.
- **Left dock — Hierarchy/Scene:** searchable object tree (`Search objects…`).
- **Status bar:** connection state (●`Disconnected`), current scene (`Untitled`),
  active tool + selection count (`Move | 0 obj | 0 sel`), FPS + frame-ms + draw
  counts. **This connection indicator is exactly where WorldForge surfaces the
  live mangos link state.**

Fonts shipped: IBM Plex Sans, Roboto, JetBrains Mono, Font Awesome (`fa-solid-900`)
for icons — see `SparkEditor/Fonts/`.

### A.2 WoW-specific layout, borrowed from Noggit Red

Noggit is viewport-dominant with chrome on three edges — overlay this onto Spark's
dock layout:

- **Center:** 3D viewport filling the window; a **circular brush ring drawn on the
  terrain surface**, conforming to contours, for radius tools.
- **Top strip:** view-toggle icons (show/hide models, WMOs, water, terrain grid,
  hole lines); active toggle highlighted.
- **Left tool rail:** one icon per editing mode — mountain = Raise/Lower,
  dashed-line = Smooth/Flatten, paintbrush = Texturing, cube = Object editor,
  plus Shader (vertex color), Water, Holes, Flags, Area-ID.
- **Right dock (context-sensitive):** the active tool's parameters — this *is*
  Spark's Inspector, re-rendered per tool.
- **Gizmo sub-toolbar** (object mode): 5 icons — gizmo visibility · world/local
  space · move · rotate · scale. Matches Spark's `GizmoSystem` 1:1 (see B.2).
- **Floating windows:** Asset/Model Browser, Tileset Chooser (texture gallery),
  numeric Position/Rotation editor.

### A.3 Brush vs. chunk cursors (a WoW detail to honor)

- **Radius tools** (sculpt, smooth, texture paint, water, vertex-shade) draw a
  **circular** ground-conforming cursor; `Alt`+drag resizes it live.
- **Chunk tools** (Holes, Area-ID, Impassable flags) operate per-MCNK and draw a
  **square per-chunk** cursor instead. Distinguish them visually.

### A.4 Camera & interaction conventions (Noggit + Spark agree)

- **Fly-cam:** WASD move + **RMB-held mouselook** (6-DoF); `O`/`P` adjust speed.
  Default to this, not an orbit cam.
- **Selection:** LMB click; LMB-drag = marquee multi-select.
- **Gizmo:** colored XYZ handles, move/rotate/scale modes, world/local toggle,
  grid + angle snapping (Spark `GizmoSystem` already implements all of this).

---

## Part B — Editor architecture, mapped from Spark Engine

Spark already solves most of the editor-shell problem. These are the components to
adopt directly (paths under `SparkEngine/SparkEditor/Source/`).

### B.1 Shell & panels (`Core/`, `Panels/`)

- `EditorApplication` + `EditorWindowManager` + `EditorLayoutManager` — dockspace,
  per-OS window (`EditorApplicationWindows.cpp` / `…Linux.cpp`).
- `EditorPanel` base + `EditorPanelFactory`; 59 panel types. WorldForge needs a
  small subset to start: **SceneView/GameView** (viewport), **Hierarchy**,
  **Inspector**, **AssetBrowser**, **TerrainEditor**, **ObjectPlacementPanel**,
  **ConsolePanel**, plus a new **ServerBridgePanel**.
- `EditorTheme` / `EditorFonts` / `EditorIcons` — the dark+teal look and FA icons.
- `CommandHistory.h` + `UndoRedo/` — global undo/redo (every edit is a command).
- Command palette (`Ctrl+P`) and `EditorCommandRegistry` — fast verb access.

### B.2 Gizmos (`Gizmos/GizmoSystem.{h,cpp}`)

Reusable as-is (DirectX-flavored; port the math to WorldForge's RHI). API:

- `enum GizmoMode { TRANSLATE, ROTATE, SCALE }`, `enum GizmoSpace { WORLD, LOCAL }`,
  `enum GizmoAxis`.
- `Render(selected, view, proj)`, `HandleMouseInput(...)` with **ray-pick** against
  handles, `ApplyTranslation/Rotation/Scale` over **multiple** selected transforms.
- Grid snap (`SetSnapToGrid`, `SetSnapSize`) and angle snap (`SetRotationSnapAngle`).

This is the move/rotate/scale gizmo Noggit's object editor needs — already built.

### B.3 Terrain tooling (`Terrain/TerrainEditor.*`)

Spark's terrain editor is the **tooling layer**; WorldForge's `terrain.*` (MCNK
heights + MCAL alpha) is the **data model**. Map them:

| Spark API | WorldForge data it should drive |
|---|---|
| `ApplySculptingTool` / `ModifyTerrainHeight(cx,cy,radius,dz,falloff)` | `MapChunk::heights` (MCVT) |
| `TerrainBrush::FalloffType` (Flat/Linear/Smooth/…) | brush falloff (matches Noggit's Type dropdown) |
| `PaintTextureWeight(cx,cy,radius,layerIndex,strength)` | `decodeAlphaMap` ↔ a writable per-layer coverage buffer → re-encode MCAL |
| `BeginTerrainOperation`/`EndTerrainOperation` + Undo/Redo | per-stroke undo |
| `SmoothTerrain`, `ApplyErosion`, `GenerateNoiseHeightmap` | procedural helpers (optional for WoW) |

**Gap to build:** WorldForge currently *decodes* MCAL (read path, done — see
`decodeAlphaMap`). Painting needs the **inverse**: keep a 64×64 8-bit working buffer
per layer, edit it with the brush, then **re-encode** to MCLY+MCAL (4-bit packed by
default; honor the 63→64 edge-duplication unless `do_not_fix_alpha_map`).

### B.4 Live-edit bridge (`Communication/LiveEditBridge.{h,cpp}`)

**The most important reuse.** Spark's bridge is the same pattern WorldForge's
`ARCHITECTURE.md` describes — adapt its target from "AreaServer" to "mangos map tick":

- Connects over **TCP** to a running server's inter-server port as a privileged
  editor client; **separate from the game protocol** (no header cipher needed).
- `enum EditorNetMessageType : uint16_t { SceneEdit=1000, LockNotify, EditorJoin,
  EditorLeave }` — a **UserDefined range (≥1000)** so it never collides with game
  opcodes. WorldForge's `EDITOR_*` op range is the analog.
- `PushEdit(EditMessage)` enqueues; a **send thread** drains; `Update()` per frame.
- `CollaborativeEditSession` + `CollaborationPanel` add multi-user editing with node
  locking + presence — future, but the framing already supports it.

WorldForge keeps its own `worldproto`-style length+opcode framing for the wire
format but copies this connect/queue/drain/ack shape verbatim.

---

## Part C — WoW 1.12.1 client RE reference (runtime/renderer)

Concrete facts the renderer and any client-side observation rely on. (Memory
offsets are for `WoW.exe 1.12.1.5875`, ImageBase `0x400000`; file formats are
ADT/WDT/WDL **v18**. Offsets cross-checked across Aesha, Fishbot-1.12.1, cMaNGOS.)

### C.1 Coordinate system (authoritative; matches `coords.hpp`)

- **World space:** right-handed, **Z-up**. **+X = North, +Y = West**, +Z = up,
  0 = sea level. Origin = map center; coords run **±17066.666** yd.
- **Grid math:** 64×64 ADT tiles; tile = **533.33333** yd (1600/3); 16×16 MCNK per
  tile → chunk = **33.3333** yd; MCVT outer spacing ≈ **4.1667** yd.
- **World↔tile** (note the X/Y swap):
  `tileX = floor(32 − worldY/533.333)`, `tileY = floor(32 − worldX/533.333)`,
  file `World/Maps/{Map}/{Map}_{tileX}_{tileY}.adt`.
- **Orientation/facing:** radians, CCW about +Z. **North = 0, West = π/2, South = π,
  East = 3π/2.**
- **Renderer caveat:** keep native Z-up; only convert at the graphics-API boundary.
  Don't silently remap `(x,y,z)→(x,−z,y)`.

### C.2 Camera & world-to-screen

- `CGWorldFrame::GetActiveCamera` @ `0x4818F0`; `RenderWorld` @ `0x482D70` is the
  per-frame render entry (the canonical injected-renderer hook).
- `CGCamera` (base `*(*(DWORD*)0x0074B2BC + 0x65B8)`): `position[3]`@`0x08`,
  3×3 orientation (rows = forward/right/up world basis)@`0x14`, `fov`@`0x38`
  (radians), near/far/aspect @`0x3C/0x40/0x44`.
- The client stores **3×3 + FOV**, not a packed 4×4 VP. Two W2S approaches:
  - **Vanilla style:** `view = (dot(d,right), dot(d,up), dot(d,forward))` with
    `d = world − camPos`; behind-camera if `view.z < 0.001`; then
    `screenX = W/2 + (view.x/(view.z·tan(xFov/2)))·W/2`, Y analogous (flipped).
  - **Generic:** build view·proj from fov/aspect/near/far, multiply, perspective-
    divide by `w` (off-screen if `w ≤ 0.001`), map NDC→pixels with Y flipped.
    This is what WorldForge's own `math.hpp`/`raster.hpp` path already does.

### C.3 Object model (for live observation / a future client-side view)

- **ObjectManager:** `s_curMgr` @ `0x00B41414`; `FirstObject` @ `mgr+0xAC`,
  `NextObject` @ `obj+0x3C`, descriptor ptr @ `obj+0x08`, **TypeID byte @ `obj+0x14`**
  (0=Object…3=Unit,4=Player,5=GameObject…), entry @ `obj+0x0C`, GUID @ `obj+0x30`.
- **GUID:** 64-bit; high word selects class (UNIT `0xF130`, GAMEOBJECT `0xF110`,
  PLAYER `0x0000`). Players have high word 0 → identify by TypeID byte == 4.
- **Position is NOT a descriptor field in vanilla** — it lives in a movement block:
  `moveData = *(obj+0x118)`; `X/Y/Z` @ `+0x10/0x14/0x18`, facing @ `+0x1C`.

### C.4 ADT rendering specifics (extends what `terrain.*` already parses)

- **Alpha splat:** layer 0 = opaque base (no map); layers 1–3 lerp on top via 64×64
  alpha. Formats: **2048-B 4-bit "small alpha" (vanilla default**, ×17 scale),
  **4096-B 8-bit "big alpha"** when WDT MPHD `adt_has_big_alpha`, **RLE** when MCLY
  flag `0x200`. *(All three already handled by `decodeAlphaMap`.)*
- **63→64 fix:** unless MCNK flag bit15 `do_not_fix_alpha_map`, duplicate the last
  row/column to avoid chunk-edge seams. *(Apply this in the renderer/re-encoder.)*
- **MCNR trailing pad:** 145×int8[3] normals are followed by **13 pad bytes outside
  the declared size** — skip them. *(`terrain.cpp` already accounts for this.)*
- **Liquid (vanilla) = MCLQ**, per-MCNK (not MH2O): min/max height, 9×9 vertex grid
  (union keyed by type: water depth+flow vs magma s/t), then 8×8 render-flags.
  Type comes from MCNK header flags. *(Not yet parsed in WorldForge — see roadmap.)*
- **Streaming:** WDT `MAIN` (64×64 × 8-B SMAreaInfo, bit0 `has_adt`) says which
  tiles exist; load the player's tile + surrounding ring; `MCIN` gives direct MCNK
  seeks. WDL = low-res distant terrain (MAOF→MARE, 545 int16 heights/tile).

### C.5 Placement encodings (two rotation conventions!)

- **MDDF** (M2 doodad, 36 B): position[3], **rotation[3] = Euler degrees**, scale
  (1024 = 1.0), flags. *(`parseAdt` already reads this.)*
- **MODF** (WMO, 64 B): position[3], **rotation[3] = Euler degrees**, AABB
  lower/upper[3], flags, doodadSet, nameSet. *(Already read.)*
- **MODD** (doodad *inside* a WMO, 40 B): **quaternion** orientation (X,Y,Z,W),
  position as (X,Z,−Y), scale, color. A live editor must handle **both** encodings.

---

## Part D — Live-server bridge to mangos-zero (the distinctive feature)

**Key result: no client/protocol changes needed.** mangos already broadcasts
authoritative state to unmodified clients via `SMSG_UPDATE_OBJECT` (create),
`SMSG_MONSTER_MOVE` (spline), `MSG_MOVE_HEARTBEAT` (position),
`SMSG_DESTROY_OBJECT` / `SMSG_GAMEOBJECT_DESPAWN_ANIM` (despawn). Mutate server-side
state on the owning thread and the existing visibility system pushes those packets.

> Target core: **`cmangos/mangos-classic`** (clean APIs, `src/game/Maps`,
> `Entities`, `MotionGenerators`). `mangoszero/server` is structurally identical
> (`src/game/WorldHandlers/`); deltas noted inline below.

### D.1 Where to drain the command queue

- Tick: `WorldRunnable::run()` (~20 Hz, `WORLD_SLEEP_CONST 50`) → `World::Update` →
  `MapManager::Update` → one `MapUpdateWorker` per map (never two for the same Map).
- **Map-scoped edits (move/spawn/despawn a creature):** drain at the **head of
  `Map::Update`**, beside the existing `GetMessager().Execute(this)` closure queue —
  lock-free (single worker owns the map), runs before the visibility pass so edits
  land the same tick. *(mangos-zero lacks `Messager`; add the drain call.)*
- **Global edits (templates/config/spawn-table writes):** drain at `World::Update`
  head / next to `ProcessCliCommands()`.
- **Never** drain inside the cell/visitor loop — iterators + marked-cell set are mid-flight.

### D.2 The operations (server methods each `EDITOR_*` op calls)

- **Resolve target:** `ObjectAccessor::GetUnit(refObj, guid)` — players are global
  (`FindPlayer`), creatures/GOs are **map-local** (`Map::GetObjectsStore()`). Editor
  addresses a target by `(mapId, ObjectGuid)`.
- **Move:** `Map::CreatureRelocation(c, x,y,z,o)` (handles cell crossing) → emits
  `SMSG_MONSTER_MOVE` via `MoveSplineInit::Launch()`; or `Unit::SendHeartBeat()` for
  a player-style `MSG_MOVE_HEARTBEAT`. `Map::GameObjectRelocation` for GOs.
- **Spawn:** `WorldObject::SummonCreature(TempSpawnSettings, map)` →
  `Creature::Create` → `Map::Add(creature)` (registers GUID, builds
  `SMSG_UPDATE_OBJECT` create block).
- **Despawn:** `Creature::ForcedDespawn(ms)` → `AddObjectToRemoveList` →
  `Map::Remove` → `SMSG_DESTROY_OBJECT`.
- **Waypoints:** `MotionMaster::MoveWaypoint(pathId, source, …)` /
  `MovePath(vector, cyclic)` (in-memory) / `MovePoint(id, x,y,z)`; persistent paths
  via `sWaypointMgr.AddNode/SetNodePosition/DeleteNode`.

### D.3 Persistence (world DB)

For edits that should survive restart, write spawn rows; the object materializes
when its grid next loads (no separate registration table):

- **`creature`** — `guid, id, map, position_x/y/z, orientation, spawntimesecs(min/max),
  spawndist, MovementType` (0 idle / 1 random / 2 waypoint).
- **`gameobject`** — `…, orientation, rotation0..3` (quaternion), `state`.
- **`creature_movement`** (PK `id`=`creature.guid`,`point`) — patrol nodes; set
  `creature.MovementType=2`.
- Optional **`creature_addon`** (mount/emote/auras).

Live MVP does **both**: mutate the in-memory object now (instant client feedback)
*and* write the DB row (durability).

### D.4 In-process command channel — copy `QueueCliCommand`

mangos already injects foreign-thread commands onto the world thread via a
mutex-guarded `std::deque<CliCommandHolder*>` (`World::QueueCliCommand` →
`ProcessCliCommands` drained in `World::Update`). Mirror it exactly:

1. A dedicated **acceptor thread** (or ACE reactor like `RASocket`) on a private,
   auth-gated port that only **enqueues**.
2. A `std::deque<WorldForgeCommand*>` + `std::mutex` clone.
3. Drain via `sWorldForge->ProcessQueue(diff)` at `Map::Update` head (object edits)
   and/or `World::Update` (global).
4. Per-command `Print`/`Finished` callbacks stream diffs/acks back (like RASocket's
   `Send` lambda) — feeds the editor's undo/ack/op-id.

**Fastest MVP:** skip a custom protocol entirely — send command strings, wrap in
`CliCommandHolder`, call `sWorld.QueueCliCommand`, and add a `.worldforge …`
subtree to `ChatHandler::getCommandTable()`. Zero new concurrency surface. Graduate
to the binary `EDITOR_*` channel for high-frequency drag operations.

> **Threading caveat:** with `MapUpdate.Threads > 0`, a creature is owned by its
> map's worker — never mutate it from the socket thread. Marshal the edit into that
> `Map`'s queue (D.1).

---

## Part E — How this ties to existing WorldForge code, and the roadmap

### E.1 What WorldForge already has (verified, in-tree)

- Asset pipeline: `mpq` (StormLib), `wow_files` (WDT/ADT/DBC), `terrain` (MCNK mesh +
  **MCAL decode**), `blp`, `m2`, `wmo`, `coords`, `math`.
- Rendering: `raster` (software, the data-path oracle) + `rhi.hpp` (backend-agnostic
  interface — the GPU backend is the gap).
- Protocol: `srp6`, `worldproto` (header cipher + framing) — the bridge's framing base.
- **Editor core (new):** `editing` (brush falloff + height/alpha brushes),
  `gizmo` (ray/transform picking, mesh hit-test, snapping), `editor_bridge`
  (`EDITOR_*` ops + framing), `db_export` (spawn-table SQL), `byte_writer`.
- 273 unit checks, all green.

### E.2 The gaps the editor needs (in dependency order)

Status: ✅ done · ◻ remaining (GPU/display- or mangos-gated, the stated ceiling).

1. ◑ **RHI backend** — ✅ `rhi_software` realises `rhi::Device` on the rasteriser
   and is unit-tested (upload → draw → readback). ✅ `rhi_gl` implements the same
   interface on OpenGL 3.3 (FBO + shader + readback), gated `WFORGE_RHI_GL` — it
   needs a GL context/loader so it builds on a desktop, not headless CI. The
   software device is the runtime-verified oracle. *(ARCHITECTURE.md §2.)*
2. ✅ **ImGui editor shell** — Dear ImGui (docking branch) + ImGuizmo vendored;
   `AtmospherePanel`, `DebugVisPanel`, an embedded `ViewportPanel` (CPU-rendered
   scene + gizmo), and a WASD fly `Camera`, all headless-tested. A **software
   ImGui backend** + the software rasteriser give a CPU/NullRHI fallback, so
   `wforge-editor-headless` composites the whole editor to a PNG with no GPU. The
   runnable windowed shell (GLFW + GL3, dockspace) is `WFORGE_EDITOR_APP` (OFF,
   desktop). Remaining: bridge the panels' emitted ops to a live socket.
3. ✅ **MCAL write path** — `encodeAlphaMap` / `packAlphaLayers` (inverse of
   `decodeAlphaMap`); round-trip tested. *(Renderer still owes the 63→64 edge fix
   at draw time.)*
4. ✅ **Terrain & object tool logic** — `editing` brushes over MCVT heights + MCAL
   coverage; `gizmo` ray-pick / transform / snap for MDDF/MODF placement. The
   ImGui/GL *presentation* of these is #2.
5. ✅ **MCLQ liquid parse** — in `terrain.*` (`MclqLayer`, `LiquidType`). *(Byte
   layout still to confirm against a real 1.12 ADT — flagged in C.4.)*
6. ◑ **Editor↔mangos bridge** — ✅ WorldForge side: `editor_bridge` op structs +
   framing. ◻ mangos side: a `.worldforge` `ChatHandler` subtree (MVP) → a queued
   binary channel drained at `Map::Update` head (D.1/D.4); needs your mangos tree.
7. ✅ **Persistence** — `db_export` emits `creature`/`gameobject`/`creature_movement`
   SQL (D.3).

### E.3 Suggested first milestone

A read-only **viewport**: GPU RHI backend renders a real ADT tile
(`buildTileMesh` + multi-layer MCAL splat) inside the ImGui shell, with WASD+RMB
fly-cam and the Spark dark/teal theme. No server yet. The data path beneath it —
mesh, texturing/encode, brushes, picking — is now all in-tree and tested; this
milestone adds the GL backend + shell on top. The live-server bridge (editor side
done) is milestone two.

---

## Sources

**WoW client RE / formats**
- https://wowdev.wiki/ADT/v18 · https://wowdev.wiki/WDT · https://wowdev.wiki/WDL/v18 · https://wowdev.wiki/WMO · https://wowdev.wiki/Camera
- https://github.com/Deamon87/WebWoWViewer/wiki/Everything-you-wanted-to-know-about-coordinate-system-in-WoW
- https://github.com/dna113p/Aesha · https://github.com/WowDevs/Fishbot-1.12.1 · https://gist.github.com/LaBlazer/442d8eed354777f5455b9014de5bc60d
- https://www.ownedcore.com/forums/world-of-warcraft/world-of-warcraft-bots-programs/wow-memory-editing/328263-wow-1-12-1-5875-info-dump-thread.html
- https://www.ownedcore.com/forums/world-of-warcraft/world-of-warcraft-bots-programs/wow-memory-editing/486791-c-worldtoscreen.html
- https://www.getmangos.eu/wiki/referenceinfo/clientfiles/adt-file-r20028/ · https://github.com/cmangos/issues/wiki/ADT-Files

**mangos-zero / cMaNGOS server**
- World loop / queue: `.../src/mangosd/WorldRunnable.cpp` · `.../src/game/World/World.cpp` · `.../src/mangosd/CliRunnable.cpp` · `.../src/mangosd/RASocket.cpp`
- Maps/grids: `.../src/game/Maps/Map.cpp` · `MapManager.cpp` · `MapUpdater.cpp` · `GridDefines.h` · `.../src/game/Grids/GridNotifiers.h`
- Entities/movement: `.../src/game/Entities/{Object,Creature,Unit,GameObject,TemporarySpawn}.cpp` · `.../src/game/Movement/{MoveSplineInit,packet_builder}.cpp` · `.../src/game/Globals/{ObjectAccessor,ObjectMgr}.cpp`
- Motion: `.../src/game/MotionGenerators/{MotionMaster,WaypointMovementGenerator,WaypointManager}.{h,cpp}`
- Schema: `.../sql/base/mangos.sql` · getMaNGOS DB wiki (creature/gameobject/creature_movement)
- Opcodes: https://wowdev.wiki/SMSG_UPDATE_OBJECT · https://wowdev.wiki/Opcodes

**Editors**
- Noggit: https://github.com/Marlamin/noggit-red · https://github.com/wowdev/noggit3 · https://deepwiki.com/wowdev/noggit3/4-editing-tools (+ 4.1/4.2/4.3)
- Map-making guides: https://marlamin.github.io/modern-map-making/ · https://wotlkdev.github.io/wiki/your_first_mod/your_first_map_edit
- Blizzard WoWEdit (ref): https://www.wowdev.wiki/WoWEdit · https://wowpedia.fandom.com/wiki/World_Editor
- Spark Engine: https://github.com/Krilliac/SparkEngine — `SparkEditor/Source/{Core,Panels,Gizmos,Terrain,Communication}`
