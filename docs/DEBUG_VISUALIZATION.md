# WorldForge — Debug Visualisation

How WorldForge renders **server-side debug data** (waypoints, pathing,
collision, line-of-sight, triggers, cells) and **locally-derived geometry**
(terrain/doodad wireframe, normals, grid, frustum) in its 3D viewport.

The principle, like the live editor bridge, is *second consumer*: the
mangos-zero server already produces this data and can show it to a retail client
as in-world markers; WorldForge consumes the **same** data and draws it natively
as proper lines / volumes / markers, which is richer and toggleable per layer.

---

## 1. Pieces in this repo

| Module | Role |
|--------|------|
| `debugdraw.{hpp,cpp}` | immediate-mode primitive buffer (lines / tris / points), category-tagged, with shape builders (aabb, box, sphere, circle, cross, arrow, path, grid, wireframe, normals, frustum) |
| `raster.cpp` `rasterDebug` | draws the overlay in the software rasteriser (near-clipped DDA lines, alpha-blended tris, point markers; optional depth test) — the runnable, headless proof |
| `editor_bridge.*` debug ops | `EDITOR_DEBUG_*` wire messages + `DebugVisType` + `apply()` that turns a decoded server message into `DebugDraw` primitives |
| `wforge-debug-demo` | renders terrain + the full overlay to `worldforge_debug.png` |

A GPU backend draws the identical `DebugDraw` buffers through a `Lines` /
`Triangles` pipeline (`rhi.hpp` already exposes `Primitive::Lines`); the software
path is the reference.

## 2. Layers (`DebugCategory`)

Each primitive is tagged with a category the editor toggles independently:

`TerrainWire`, `DoodadWire`, `Collision`, `Waypoint`, `NavMesh`, `NavPath`,
`Trigger`, `Marker`, `Normal`, `Grid`, `Frustum`, and — mirroring the server —
`Cell`, `LineOfSight`, `HitPoint`, `Height`.

`DebugDraw::setCategoryEnabled(cat, on)` drives the "show pathing / colliders /
triggers / wireframe" checkboxes; `stats()` reports per-frame line/tri/point
counts for enabled layers only (matching Spark's `DebugDrawStats`).

## 3. Server alignment — mangoszero/server PR #386

PR #386 (`.debug vis`) adds a server-side debug-draw toolkit that spawns
colour-coded temporary gameobjects (a reserved GOOBER pool) with hover tooltips,
emitted by:

- `.debug vis cells [radius]` — grid cell markers → `DV_CELL`
- `.debug vis los` — line-of-sight to target → `DV_LOS_OK` / `DV_LOS_BLOCK`
- `.debug vis path` — navmesh path to a unit → `DV_PATH` / `DV_PATH_BAD`
- `.debug vis collision [dist]` — collision ray ahead → `DV_COLLISION` + `DV_HITPOINT`
- `.debug vis height` — ground elevation → `DV_HEIGHT`
- `.debug vis clear` — remove markers

WorldForge's `DebugVisType` enum carries those exact `DV_*` types on the wire;
`categoryFor()` maps each to a render layer, and `apply()` builds the geometry:

| Server `DV_*` | `DebugVisType` | WorldForge render |
|---------------|----------------|-------------------|
| `DV_CELL` | `Cell` | box/AABB per cell (`EDITOR_DEBUG_VOLUME`) |
| `DV_LOS_OK` / `DV_LOS_BLOCK` | `LosOk` / `LosBlock` | coloured ray + hit (`EDITOR_DEBUG_LINE`) |
| `DV_PATH` / `DV_PATH_BAD` | `Path` / `PathBad` | polyline + node markers (`EDITOR_DEBUG_PATH`) |
| `DV_COLLISION` | `Collision` | ray (`EDITOR_DEBUG_LINE`) |
| `DV_HITPOINT` | `HitPoint` | point marker |
| `DV_HEIGHT` | `Height` | marker + captured value/label (`EDITOR_DEBUG_MARKER`) |

**Integration contract (server side, not in this tree):** the `DebugVis`
subsystem that already captures this data (hit coords, path point/type, height
delta) gains a parallel emit over the editor channel — frame the capture with
`EDITOR_DEBUG_*` and send it down the existing editor socket, in addition to (or
instead of) spawning the GO marker. WorldForge decodes and `apply()`s it. No
game-protocol change; this rides the trusted editor link (see
`EDITOR_RESEARCH.md` part D).

## 4. Live-override commands (deferred, separate track)

`Krilliac/server-Zero@claude/live-override-commands-plan` plans a GM command
suite (`.light`, `.weather`, `.music`, `.spellvisual`, `.cinematic`,
`.worldstate`, `.screenmsg`, …) that pushes atmosphere/spectacle/HUD state to
retail clients via existing SMSG packets, with `self|target|zone|server` scopes.

That is **deferred** and is *not* debug-draw — it has no viewport geometry. When
it lands it surfaces in WorldForge as an **Atmosphere/World panel** (light/weather/
time sliders, a scope selector) whose actions ride the same editor bridge as
command strings (the `QueueCliCommand` MVP path in `EDITOR_RESEARCH.md` D.4),
not as `DebugDraw` primitives. Tracked here so the panel slots in cleanly later.

## 5. Status

Implemented + unit-tested in-tree: the primitive buffer + builders, the software
rasteriser overlay (with `wforge-debug-demo` as the visual proof), the
`EDITOR_DEBUG_*` protocol, and the `DV_*` mapping/apply. Gated by the project's
stated ceiling (no GPU/display, no mangos tree here): the GPU `Lines`/`Triangles`
backend, the ImGui layer-toggle panel, and the server-side parallel emit.
