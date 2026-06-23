# WorldForge

A from-scratch World of Warcraft **1.12.1 (vanilla)** client/world engine in
modern C++, reconstructed from documented file formats and verified against
[wowdev.wiki](https://wowdev.wiki) and the getMaNGOS/cmangos references. Built to
pair with a mangos-zero server: load real client assets, render the world in a
3D viewport, and — the distinctive goal — **author the live world against a
running server while connected retail clients see the edits.**

Every parser, transform, and protocol primitive here is **compiled and
unit-tested in-tree** (114→176 checks). Where a part needs a GPU/display or your
mangos checkout to run, it is implemented as far as it can be verified and the
boundary is stated plainly (see `ARCHITECTURE.md`).

## Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/wforge-tests          # run the unit tests
./build/wforge-raster-demo    # render procedural terrain -> PNG
./build/wforge-dump <DataDir> <Map> [tileX tileY]   # inspect a real MPQ tree
```

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
| `wow_files.*`     | WDT / ADT placement / DBC parsing                              |
| `terrain.*`       | MCNK heightmap/normals/layers → hole-aware mesh; MCAL alpha unpack |
| `image.*`         | RGBA image + dependency-free PNG writer                        |
| `blp.*`           | BLP2 texture decode (palette / DXT1/3/5 / raw) → RGBA          |
| `m2.*`            | M2 model: static mesh + vanilla skeletal animation parse       |
| `anim.hpp`        | keyframe tracks + slerp + bone-hierarchy pose composition      |
| `wmo.*`           | WMO root + group parse (geometry, materials, doodads)          |
| `crypto.*`        | SHA-1 + arbitrary-precision modpow                             |
| `srp6.*`          | WoW-flavour SRP6 logon (client + server)                      |
| `worldproto.hpp`  | vanilla header cipher + opcode framing                        |
| `raster.*`        | software rasteriser (z-buffer, perspective-correct)            |
| `rhi.hpp`         | GPU render-hardware interface (backend-agnostic)              |

## Status at a glance

Implemented + tested: MPQ/WDT/ADT/DBC, coordinate transforms, terrain meshing,
BLP, M2 (static + animation), WMO, SRP6 logon, header cipher, framing, software
rendering. Design + compile-only: GPU backend, live-server editor bridge — see
`ARCHITECTURE.md` for the integration contract and the verified-vs-architectural
table, and `docs/EDITOR_RESEARCH.md` for the editor UI/architecture design,
WoW client RE reference, mangos-zero hook points, and the build roadmap.

## Provenance

Reconstructed purely from public format documentation and clean-room parsing of
user-supplied client files. No Blizzard code or copyrighted assets are included
or required to build; the rasteriser demo uses a procedural heightfield.
