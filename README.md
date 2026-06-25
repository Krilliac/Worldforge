# WorldForge

A from-scratch World of Warcraft **1.12.1 (vanilla)** client/world engine in
modern C++, reconstructed from documented file formats and verified against
[wowdev.wiki](https://wowdev.wiki) and the getMaNGOS/cmangos references. Built to
pair with a mangos-zero server: load real client assets, render the world in a
3D viewport, and — the distinctive goal — **author the live world against a
running server while connected retail clients see the edits.**

Every parser, transform, and protocol primitive here is **compiled and
unit-tested in-tree** (114→577 core + 76 editor checks). Where a part needs a GPU/display or your
mangos checkout to run, it is implemented as far as it can be verified and the
boundary is stated plainly (see `ARCHITECTURE.md`).

## Build

```bash
git clone --recurse-submodules <repo>          # Dear ImGui is a submodule
cmake -S . -B build
cmake --build build -j$(nproc)
./build/wforge-tests             # core unit tests
./build/wforge-editor-tests      # editor panel tests (headless ImGui)
./build/wforge-raster-demo       # render procedural terrain -> PNG
./build/wforge-debug-demo        # render terrain + debug overlay -> PNG
./build/wforge-editor-headless   # render the whole editor on the CPU -> PNG
./build/wforge-scene-demo        # terrain + textured animated model instances -> PNG
./build/wforge-model-viewer      # animated M2 in a model-viewer panel -> PNG
./build/wforge-terrain-demo      # textured terrain (MCAL grass+rock splat) -> PNG
./build/wforge-stub-server [port]   # run the standalone bridge server (no mangos)
./build/wforge-dump <DataDir> <Map> [x y] [--render tile.png] [--dbc <Name>]   # inspect/render a real MPQ tile (zone-lit, with sky)
./build/wforge-m2dump <DataDir> <archived\path.m2> [out.png]   # parse + render one real M2 model
./build/wforge-flythrough <DataDir> <Map> <cx> <cy> [N] [out.png]   # stitch an NxN block of real tiles -> one zone-lit scene with sky
./build/wforge-minimap <DataDir> <Map> [out.png] [cell]   # whole-continent elevation minimap from the .wdl heightfield
```

Real-asset rendering uses the client's `Light.dbc` for zone-appropriate lighting and a
sky backdrop; pass a WoW install root (next to `Wow.exe`), a `Data` dir, or `.` for the
`<DataDir>` argument. Multi-expansion is scaffolded behind `ClientProfile`
(`src/client_version.hpp`, threaded through the M2/WMO parsers); only vanilla 1.12.1 is
implemented and every other version fails loud.

No GPU? The editor has a **CPU/NullRHI fallback**: the software rasteriser
(`rhi_software`) draws the 3D scene and a software ImGui backend
(`editor/SoftwareImGui`) draws the UI, so `wforge-editor-headless` composites the
whole editor to a PNG with no display (the analog of Spark's llvmpipe path).

If you already cloned without submodules: `git submodule update --init --recursive`.
The ImGui editor panels build headlessly (`WFORGE_EDITOR`, ON). The runnable
windowed editor needs a GL/GLFW toolchain + display and is opt-in:
`-DWFORGE_EDITOR_APP=ON` (add GLFW: `git submodule add https://github.com/glfw/glfw external/glfw`).

The windowed editor (`wforge-editor-app`) takes an optional client + tile:
`wforge-editor-app <DataDir> [Map x y]` (or `$WFORGE_CLIENT` / `$WFORGE_TILE_MAP`,
`$WFORGE_TILE_X`, `$WFORGE_TILE_Y`). With a client mounted it shades the viewport
from the real `Light.dbc`; given a tile it renders that real ADT (textured terrain
+ doodads + WMOs + liquid) instead of the procedural placeholder. The **Sky** panel
scrubs the live day-tick (or auto-advances dawn→dusk), re-lighting the scene. With
no client found, it falls back to procedural terrain under the legacy grey light.

Requires CMake ≥ 3.12 and a C++17 compiler (tested GCC 13; MSVC `/W4
/permissive-` and GCC `-Wall -Wextra` both clean). StormLib is fetched
automatically via CMake FetchContent.

## Module map (`src/`)

| File              | What it does                                                    |
|-------------------|----------------------------------------------------------------|
| `math.hpp`        | Vec/Mat4 (column-major), Quat + slerp, camera, projection      |
| `coords.hpp`      | WoW coordinate constants + world↔placement transforms          |
| `byte_reader.hpp` / `chunk.hpp` | bounds-checked LE reader; IFF chunk iterator     |
| `mpq.*`           | StormLib MPQ archive chain with patch-override priority         |
| `wow_files.*`     | WDT / ADT (MTEX/placement) / DBC parsing                        |
| `dbc_defs.*`      | typed DBC views (Map / AreaTable / LiquidType / Light)         |
| `gridmap.*`       | mangos `.map` parse (server height/area/liquid/holes grid)     |
| `navmesh.*`       | mangos `.mmtile` Detour navmesh parse → wireframe overlay      |
| `terrain.*`       | MCNK heightmap/normals/layers → hole-aware mesh; MCAL alpha unpack |
| `image.*`         | RGBA image + dependency-free PNG writer                        |
| `blp.*`           | BLP2 texture decode (palette / DXT1/3/5 / raw) → RGBA          |
| `m2.*`            | M2 model: static mesh + vanilla skeletal animation parse       |
| `anim.hpp`        | keyframe tracks + slerp + bone-hierarchy pose composition      |
| `wmo.*`           | WMO root + group parse (geometry, materials, doodads)          |
| `crypto.*`        | SHA-1 + arbitrary-precision modpow                             |
| `srp6.*`          | WoW-flavour SRP6 logon (client + server)                      |
| `worldproto.hpp`  | vanilla header cipher + opcode framing                        |
| `clientfx.*`      | vanilla server-FX packet builders (sound/weather/cinematic/world-state) |
| `fxbridge.*`      | scope-aware Atmosphere/World FX editor RPCs → realise to clientfx SMSG |
| `raster.*`        | software rasteriser: shaded mesh + textured (UV) mesh          |
| `m2_render.*`     | M2 skinning: bone pose → posed textured mesh (animation)       |
| `scene.*`         | compose terrain + textured model instances + overlay → render  |
| `terrain_render.*`| MCNK/MCAL multi-layer alpha-splat textured terrain             |
| `asset_loader.*`  | MPQ → BLP/WDT/ADT → textured tile (the real-asset pipeline)     |
| `rhi.hpp`         | GPU render-hardware interface (backend-agnostic)              |
| `rhi_software.*`  | RHI realised on the rasteriser (tested headless)               |
| `rhi_gl.*`        | RHI realised on OpenGL 3.3 (gated `WFORGE_RHI_GL`, desktop)    |
| `vmap.*`          | VMAP `.vmo` collision parse → wireframe; `mpq.*` patch writer  |
| `editing.*`       | brush falloff + terrain-height / alpha-coverage edit tools      |
| `gizmo.*`         | ray/transform picking, mesh hit-test, snapping (move/rotate/scale) |
| `editor_bridge.*` | editor↔server RPC: `EDITOR_*` op structs + `.debug vis` stream |
| `db_export.*`     | placements → mangos `creature`/`gameobject`/`creature_movement` SQL |
| `debugdraw.*`     | category-tagged debug primitives (waypoints/collision/triggers/wireframe) |
| `modelmesh.*`     | M2 / WMO geometry → Mesh + textured TexMesh                     |
| `byte_writer.hpp` | little-endian write counterpart to `byte_reader.hpp`           |
| `editor/`         | ImGui panels + Viewport + fly Camera + ImGuizmo + software ImGui + bridge client |
| `server/`         | WorldSim + standalone stub bridge server (runnable WorldForgeBridge analog) |

## Status at a glance

Implemented + tested: MPQ/WDT/ADT/DBC, coordinate transforms, terrain meshing,
BLP, M2 (static + animation), WMO, SRP6 logon, header cipher, framing, software
rendering. Design + compile-only: GPU backend, live-server editor bridge — see
`ARCHITECTURE.md` for the integration contract and the verified-vs-architectural
table, and `docs/EDITOR_RESEARCH.md` for the editor UI/architecture design,
WoW client RE reference, mangos-zero hook points, and the build roadmap.
`docs/DEBUG_VISUALIZATION.md` covers rendering server-side debug data
(waypoints/pathing/collision/triggers, aligned with mangoszero `.debug vis`).
`docs/SERVER_OPCODES.md` documents the verified vanilla server-FX opcode
builders, plus using `SMSG_OVERRIDE_LIGHT` as a server-handled custom opcode
over the trusted editor↔server link (it is not a vanilla *client* render path).
`integration/mangos-zero/` is a drop-in server-side bridge module sketch that
applies the editor RPCs to the live world. `docs/SERVER_DATA.md` covers the
mangos extractor outputs (`.map`/`.vmap`/`.mmap`/DBC) — what WorldForge reads
in-engine vs streams live vs edits on the source asset.

## Provenance

Reconstructed purely from public format documentation and clean-room parsing of
user-supplied client files. No Blizzard code or copyrighted assets are included
or required to build; the rasteriser demo uses a procedural heightfield.
