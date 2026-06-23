# Server data: maps / vmaps / mmaps / DBC — visualize & edit strategy

Where the mangos **extractor outputs** fit in WorldForge, and the rule for
whether a thing is read **in-engine from a file** vs **streamed live from the
server** vs **edited on the source asset**.

## The two sources of truth

WorldForge already parses the **client source assets** straight from the MPQs:
ADT/WDT terrain, M2/WMO geometry, BLP textures, DBC. The mangos extractors
(`map-extractor`, `vmap-extractor`/assembler, `MoveMapGen`) take those *same*
client assets and bake **derived** server files: `.map`, `.vmap`, `.mmap`. So:

```
 client MPQ assets  ──parse──►  WorldForge (authoritative source)
        │
        └──mangos extractors──►  .map / .vmap / .mmap  (derived, server view)
                                        │
                                        └── loaded by mangos at runtime
```

**Editing always happens on the source, never on the derived file.** You edit an
ADT height / a WMO placement / a DBC row in WorldForge; the `.map/.vmap/.mmap`
are then *regenerated* by the extractor (or the server reloads). You never
hand-edit a baked `.mmap` — it's an output, not an input.

## What to read from where

| Data | Best source in WorldForge | Why |
|------|---------------------------|-----|
| Terrain height / normals / holes | **client ADT** (`terrain.*`, already done) | WorldForge computes it from source; richer than the server's baked grid |
| Liquid surfaces | **client ADT MCLQ** (done) + optional `.map` liquid | source has full detail |
| Server's *baked* height/area/liquid grid | **`.map` (GridMap) parse** | to **diff what the server actually sees** vs the client — debugging discrepancies |
| Collision geometry (LoS) | **WMO/WMO+M2 from source** (`modelmesh`, done) *or* `.vmap` | source gives the meshes; `.vmap` gives the *exact* server collision set + BIH |
| Navigation mesh / paths | **`.mmap`/`.mmtile` parse** (Detour) | WorldForge can't derive this itself — Recast/Detour generation is the extractor's job |
| Live LoS / path / collision *query* results | **debug bridge** (`EDITOR_DEBUG_*`, done) | dynamic, per-unit "what is the server computing right now" — mangoszero PR #386 |
| Names / ids (areas, maps, liquids, lights) | **client DBC** (`Dbc`, parser done; typed views) | the editor's id↔name resolution |

### The key distinction you asked about

- **Static / whole-tile visualization & inspection → parse the file in-engine.**
  Load `.mmap`/`.vmap`/`.map` to draw the *complete* navmesh / collision / server
  grid as overlays (feeds `DebugCategory::NavMesh / Collision / Cell`). Good for
  "show me the whole picture for this tile," offline, no server needed.
- **Dynamic / query-specific → stream from the live server.** "Why is *this* NPC
  pathing here?" The server runs the actual mmap/vmap query and streams the
  result down the debug bridge (already wired). Ground truth for the running sim.

Both feed the **same** `DebugDraw` overlay and the same viewport — they're just
two producers (a file loader vs the bridge) for the same render path.

## DBC

WorldForge has a generic `Dbc` record reader. The editor needs **typed views** to
resolve ids ↔ names/params:

- **Map.dbc** — map id → directory + display name (tile picker, map list).
- **AreaTable.dbc** — area id → name + parent + exploration level (the area-id
  paint tool, zone labels).
- **LiquidType.dbc** — liquid type → name (water tool).
- **Light.dbc** — light ids + falloff per map position (the override-light picker;
  also the vanilla-safe target for the `.light` custom op, since the client has
  no per-player override-light opcode — see `SERVER_OPCODES.md`).

DBC **editing** (custom content) writes a modified DBC into a **patch MPQ** that
the client loads — a client-side change, distinct from the live-server edits.

## Roadmap

| Piece | Status |
|-------|--------|
| Client ADT terrain / liquid / placement | ✅ done (`terrain.*`, `wow_files.*`) |
| M2/WMO → collision wireframe (from source) | ✅ done (`modelmesh` → `DebugDraw`) |
| Live debug stream (path/LoS/collision/cells) | ✅ done (`editor_bridge` `EDITOR_DEBUG_*`) |
| **GridMap `.map` parser** (server height/area/liquid/holes) | ✅ done (`gridmap.*`) |
| **Typed DBC views** (Map/AreaTable/LiquidType/Light) | ✅ done (`dbc_defs.*`) |
| **Detour `.mmtile` parser** (navmesh overlay) | ✅ done (`navmesh.*` → `addNavMesh`) |
| **VMAP `.vmtree`/`.vmtile` parser** (exact server collision) | ◻ later (source meshes cover most cases) |
| DBC → patch-MPQ writer (custom content) | ◻ later |

`gridmap`, `dbc_defs`, and `navmesh` (the byte-exact `.map` / DBC / `.mmtile`
parsers) are implemented and unit-tested. `navmesh::addNavMesh` draws the server
navmesh polygons into the `NavMesh` debug layer, so the viewport renders the
*exact* navigation graph the running server pathfinds on. VMAP collision and the
DBC→patch-MPQ writer remain the explicit follow-ups (source meshes already cover
collision wireframe for most cases).
