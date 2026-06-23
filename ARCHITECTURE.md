# WorldForge — Architecture

This document covers the parts of the engine that are **design + compile-only**
(they need a GPU/display or your mangos-zero tree to run), as distinct from the
asset/protocol pipeline, which is implemented and unit-tested in-tree.

---

## 1. Layering

```
            ┌────────────────────────────────────────────┐
            │  Editor / Runtime (3D authoring + viewport)  │
            └───────────────┬───────────────┬──────────────┘
                            │               │
                    ┌───────▼──────┐  ┌──────▼────────────┐
                    │  Renderer    │  │  Editor Bridge     │
                    │  (RHI)       │  │  (live server RPC) │
                    └───────┬──────┘  └──────┬────────────┘
                            │                │
        ┌───────────────────▼───────┐  ┌─────▼───────────────────┐
        │  Asset pipeline (tested)   │  │  World protocol (tested) │
        │  MPQ/WDT/ADT/MCNK/BLP/     │  │  SRP6 + header cipher +  │
        │  M2/WMO/anim/coords/math   │  │  opcode framing          │
        └────────────────────────────┘  └──────────────────────────┘
```

The asset pipeline and protocol layers are the verified foundation. The
renderer and editor bridge sit on top and are the parts described here.

---

## 2. Renderer (RHI)

`rhi.hpp` defines a backend-agnostic device interface: buffers, textures, named
pipelines (`"terrain"`, `"m2_opaque"`, `"wmo"`), and a `beginFrame/draw/endFrame`
loop driven by a `FrameParams` (view, proj, light). The **software rasteriser**
(`raster.hpp`) is a working realisation of the exact same data path — mesh +
MVP → shaded pixels — and is what runs in a headless/CI environment. It also
backs `readback()`-style tests.

A GPU backend (OpenGL 3.3 core is the natural first target for 1.12-era content)
implements `rhi::Device`:

- **Terrain** — one VBO per ADT tile (the 145-vertex MCNK meshes merged), indexed
  triangle lists from `buildTileMesh`. Texture layers via the MCLY/MCAL alpha
  splat; `decodeAlphaMap` unpacks each layer's 64×64 coverage map (vanilla
  packed 4-bit, 8-bit big-alpha, and RLE-compressed forms). World transform is baked into
  vertex positions (already in WoW Z-up world space), so the vertex shader only
  applies view·proj.
- **M2 doodads** — instanced. Static mesh from `parseM2` (view 0 lookup +
  triangles + submeshes). Skinned variants upload the per-frame bone matrices
  from `computePose` as a uniform array; the vertex shader blends up to 4 bones
  using the per-vertex weights/indices already parsed into `M2Vertex`.
- **WMO** — per-group VBOs (`parseWmoGroup`), batched by `MOBA` material id;
  doodad sets placed with the quaternion transforms from `MODD`.

Why an RHI rather than calling GL directly: it isolates the ABI so the same scene
graph can target D3D11 (Windows-native, matches the original client era) or
Vulkan later, and lets the software path serve as a reference/oracle for the GPU
path during bring-up.

> **Verification ceiling:** the RHI interface compiles; a concrete GPU backend
> cannot be runtime-verified in this environment (no GPU/display). The software
> rasteriser is the runnable proof of the data path.

---

## 3. Live-server editor bridge

The distinctive capability: **author the world in a 3D viewport against a
running mangos-zero, while connected 1.12.1 clients see edits live.** This is
what separates WorldForge from an offline map editor (Noggit) or a static
viewer.

### 3.1 Data flow

```
  [3D viewport]                         [WorldForge editor host]
  drag an NPC ──► EditOp{move, guid, newPos}
                          │
                          ▼  (editor RPC channel, not the game protocol)
                 ┌──────────────────────┐
                 │ command queue (MPSC)  │   one producer per editor action,
                 │  on the map thread    │   single consumer = map tick
                 └──────────┬───────────┘
                            ▼  applied during Map::Update tick
              server world state mutated (Creature::Relocate, etc.)
                            │
                            ▼  server's normal broadcast path
        SMSG_MONSTER_MOVE / SMSG_UPDATE_OBJECT to in-range clients
                            │
                            ▼
                 retail 1.12.1 clients move the NPC — no client mod
```

The key insight: we don't invent a client-side rendering path for edits. We
mutate authoritative server state and let mangos' **existing** grid/visibility
system emit the same packets it always would, so unmodified clients react. The
editor's own 3D view is a second consumer of the same state.

### 3.2 Editor RPC channel

A side channel distinct from the game opcode stream (so it can't be reached by
game clients):

- Transport: local socket / named pipe between the editor process and an
  in-process mangos module.
- Framing: reuse `worldproto`-style length+opcode framing with an `EDITOR_*`
  opcode range, but **no** header cipher (trusted local link).
- Operations (initial set): `MOVE_OBJECT(guid, pos, orient)`,
  `SPAWN_CREATURE(entry, pos)`, `DESPAWN(guid)`, `SET_WAYPOINTS(guid, path[])`,
  `EDIT_GOBJECT(guid, pos, state)`. Each is idempotent and carries a client-side
  op-id for ack/undo.

### 3.3 Threading model

| Thread          | Owns                                   | Talks to                       |
|-----------------|----------------------------------------|--------------------------------|
| Editor UI       | viewport, gizmos, selection            | enqueues `EditOp` → command q  |
| Render          | RHI device, draws scene                | reads immutable scene snapshot |
| Map tick (mangos)| authoritative world state             | drains command q, applies ops  |
| Asset I/O       | MPQ reads, BLP decode, mesh build      | fills caches (lock-free handoff)|

The command queue is the only writer into server state and is drained exactly
once per map tick, so edits are serialised against the simulation — no locks on
the hot movement path. The render thread consumes a per-frame snapshot
(double-buffered scene) so authoring never stalls drawing.

### 3.4 mangos-zero integration points

These are the hooks (in your tree) the bridge attaches to:

- **`Map::Update`** — drain the editor command queue at the top of the tick.
- **`Creature::Relocate` / `GameObject` setters** — already emit the right
  broadcasts; the bridge calls these rather than poking fields directly, so
  visibility/grid bookkeeping stays correct.
- **`ObjectAccessor` / GUID lookup** — resolve an editor selection (guid) to the
  live object.
- **Movement generators** — `SET_WAYPOINTS` installs a waypoint movement
  generator so the edited path plays back to clients exactly as a scripted one.

> **Verification ceiling:** this layer compiles against the WorldForge side
> (framing, op structs, queue) but cannot be built or run here without your
> mangos-zero checkout. The op set and hook list above are the integration
> contract; the protocol framing it reuses **is** unit-tested.

---

## 4. What is verified vs. architectural

| Subsystem                              | Status                          |
|----------------------------------------|---------------------------------|
| MPQ / WDT / ADT / DBC parse            | implemented, unit-tested        |
| Coordinate transforms (world↔placement)| implemented, round-trip tested  |
| Terrain MCNK → mesh (hole-aware)       | implemented, unit-tested        |
| MCAL alpha-map unpack (4-bit/8-bit/RLE)| implemented, unit-tested        |
| BLP2 decode (palette/DXT1/3/5/raw)     | implemented, unit-tested        |
| PNG writer                             | implemented, PIL-validated      |
| M2 static mesh parse                   | implemented, unit-tested        |
| M2 skeletal animation (core + parse)   | implemented, unit-tested        |
| WMO root + group parse                 | implemented, unit-tested        |
| SHA-1 / BigUInt modpow                 | implemented, vector-tested      |
| SRP6 logon (client+server)             | implemented, handshake-tested   |
| Vanilla header cipher + framing        | implemented, round-trip tested  |
| Software rasteriser                    | implemented, renders to PNG     |
| RHI interface                          | compiles; GPU backend = design  |
| Live-server editor bridge              | design + framing; needs mangos  |

The flagged runtime-verify items (confirm against a real 1.12.1 client/MPQ):
MCNK `IndexX/IndexY` → row/col axis; MODF extents min/max order; M2 bone-header
padding and rotation-quaternion storage (float vs int16); sequence record stride.
These are isolated behind named constants/comments so a single real file
confirms or adjusts them without touching parser logic.
