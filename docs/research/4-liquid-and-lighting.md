# Liquid (MCLQ) Surface Meshing & Zone Lighting (Light.dbc) — Vanilla 1.12.1

Research note for WorldForge. READ-ONLY analysis; no code changed.
Cross-checked against wowdev.wiki, Noggit3 source, WoWMapViewer/WoWModelViewer
(cleverca22 fork), cmangos/mangos-zero, WoWFormatLib, and a community DBC
template. Conflicts between sources are called out explicitly in section 5.

---

## 1. Summary

Two related "make the world look real" features, both pure **client data** (no
server involvement — confirmed: cmangos `DBCStores.cpp` loads neither Light.dbc
nor the band tables):

* **ADT liquid (MCLQ)** — vanilla stores water/ocean/magma/slime as a per-MCNK
  sub-chunk: a 9×9 height grid + 8×8 render mask + per-vertex depth (water) or
  texcoords (magma). WorldForge already parses this into `MclqLayer`
  (`src/terrain.hpp`/`terrain.cpp`), but discards depth/flow and never builds a
  surface mesh. The goal here is to emit a **translucent water surface mesh**
  into the tile scene, depth-faded at the shoreline, the way the real client and
  Noggit do.
* **Zone lighting (Light.dbc + bands)** — the client picks a light by world
  position (distance vs. inner/outer falloff radii) and reads time-of-day color
  curves out of `LightIntBand.dbc` (18 int "bands": ambient, diffuse/sun, sky
  gradient, fog, water colors) and `LightFloatBand.dbc` (6 float bands: fog
  distance/multiplier). These colors drive ambient + diffuse + fog + the water
  tint. WorldForge already parses `Light.dbc` into `LightEntry` but has **no
  band tables and no position→color lookup**; the scene uses a single hardcoded
  `Scene::lightDir`. The goal is a `lightingAt(worldPos, time)` lookup feeding
  `Scene` (ambient/diffuse/fog/water tint), validated by rendering a known
  watery tile (e.g. an Elwynn/Westfall river ADT, or a Stranglethorn coastline)
  from the real 1.12.1 client.

Why it matters: liquid + lighting are the two biggest remaining visual gaps for
offline tile rendering. Both are self-contained, validate against a real client
offline, and reuse the existing software shading path (`src/raster.cpp`,
`Scene::lightDir`).

---

## 2. Concrete format facts (vanilla 1.12.1 / ADT v18)

### 2.1 MCNK header liquid flags & MCLQ offset

In the MCNK header (`src/terrain.cpp` already reads these):

| Bit    | Meaning | WorldForge const |
|--------|---------|------------------|
| 0x0004 | river (water) | `MCNK_LQ_RIVER` |
| 0x0008 | ocean         | `MCNK_LQ_OCEAN` |
| 0x0010 | magma (lava)  | `MCNK_LQ_MAGMA` |
| 0x0020 | slime         | `MCNK_LQ_SLIME` |

`ofsMCLQ` is at header offset **0x60**, `sizeMCLQ` at **0x64**
(WorldForge: `kOffOfsMCLQ = 0x60`). `sizeMCLQ == 0` ⇒ no liquid even if a flag
is set; the flags above select which *union interpretation* the MCLQ vertices
use. (wowdev.wiki ADT/v18; matches Noggit3 `MapChunkHeader.ofsLiquid`/`sizeLiquid`.)

### 2.2 MCLQ chunk layout (the canonical struct)

From **Noggit3 `src/noggit/MapHeaders.h`** (verbatim) — this is the
authoritative classic layout and matches wowdev.wiki and the older
WoWMapViewer `SWVert/SLVert` union one-for-one:

```c
struct water_vert {          // when MCNK flag = river/ocean (0x4 / 0x8)
  uint8_t depth;             // 0..255, index into client depth/alpha table
  uint8_t flow_0_pct;        // flow vector 0 strength
  uint8_t flow_1_pct;        // flow vector 1 strength
  uint8_t filler;
};
struct magma_vert {          // when MCNK flag = magma/slime (0x10 / 0x20)
  uint16_t x;                // S texcoord, *raw* (see scale below)
  uint16_t y;                // T texcoord
};
struct mclq_vertex {
  union { water_vert water; magma_vert magma; };  // 4 bytes
  float height;                                   // 4 bytes  -> 8 bytes/vertex
};
struct mclq_tile {           // 1 byte bitfield per 8x8 cell
  uint8_t liquid_type : 3;   // 0..6 liquid kind; value 7 (0b111) == "no render"
  uint8_t dont_render : 1;   // bit 0x08
  uint8_t flag_0x10   : 1;
  uint8_t flag_0x20   : 1;
  uint8_t fishable    : 1;   // bit 0x40
  uint8_t fatigue     : 1;   // bit 0x80  (deep ocean "fatigue" zone)
};
struct mclq {
  float       min_height;          // +0x00
  float       max_height;          // +0x04
  mclq_vertex vertices[9 * 9];     // +0x08  (81 * 8 = 648 bytes)
  mclq_tile   tiles[8 * 8];        // +0x290 (64 bytes)
  uint32_t    n_flowvs;            // +0x2D0  number of valid flow vectors (0..2)
  mclq_flowvs flowvs[2];           // SWFlowv: sphere(center vec3, radius f32) +
                                   //          dir(vec3) + velocity f32 + amplitude
                                   //          + frequency  (0x38 bytes each)
};
```

Sizes: header 8 B + 648 B verts + 64 B tiles = **720 B**, then
`n_flowvs` (4 B) + up to 2×0x38 flow vectors. WorldForge currently reads exactly
the first 720 bytes and stops — **correct for a static surface**; only the
shoreline-foam/flow animation needs the trailing flow data, which can stay
ignored for offline stills.

**Tile-flag "render" test:** WorldForge's `liquidTileRenders()` checks
`(flag & 0x0F) != 0x0F`. That is the classic test — `0x0F` low nibble means
`liquid_type==7 && dont_render`. Equivalent and fine. (Noggit uses
`!dont_render` on the bitfield; same result.) The high nibble bits 0x40/0x80
(fishable/fatigue) are gameplay-only and irrelevant to the mesh.

**Magma texcoord scale (important for lava):** the raw `magma.x/y` uint16 must
be scaled by `MAGMA_SCALE_ADT = 3.0 / 256.0` (WMO uses `1.0/256.0`) to get UVs.
WorldForge currently drops these (`r.skip(4)`); needed only when texturing lava.

**Water depth → alpha:** per-vertex `water.depth` (0..255) is the *transparency*
driver. Noggit normalizes `depth/255` and additionally derives a shoreline
opacity from `(waterHeight - terrainHeight)`:
`opacity = clamp((diff + 1.0) * factor, 0, 1)` — i.e. water goes transparent
where it is shallow (meets the bank) and opaque where deep. This is the key
visual you want to reproduce; `min_height`/`max_height` and the per-vertex
height let you compute `diff` against the MCVT terrain you already have.

**Geometry placement:** the 9×9 liquid grid lies on the *same* outer-grid
footprint as the MCNK's 9×9 MCVT corners. World XY of liquid vertex (row r,
col c) = MCNK outer-vertex XY (the same X/Y you already compute in
`buildChunkMesh` for the 9×9 outer ring); only Z differs (use `mclq_vertex.height`,
clamped to `[min_height, max_height]`, instead of terrain height). Cell spacing
= `CHUNK_SIZE/8 = UNITSIZE` (≈4.1667 yd). So you can reuse the existing
outer-grid XY math and just swap Z.

### 2.3 LiquidType.dbc (color/visual class)

`LiquidTypeEntry` (WorldForge already has it; matches cmangos
`LiquidTypeEntry`): `Id(0), LiquidId(1), Type(2)=0 magma/2 slime/3 water,
SpellId(3)`. LiquidId examples: **23 water, 29 ocean, 35 magma, 41 slime, 47
Naxx slime**. In vanilla the MCLQ→type comes from the **MCNK flags**, not from
LiquidType.dbc (that table is mainly a 1.12 lookup; the per-tile `liquid_type:3`
field is a sub-index). For offline color you mostly need: water/ocean → blue
tint from lighting water-color bands; magma → orange/emissive; slime → green.

### 2.4 Light.dbc (vanilla)

Field layout (every field 4 bytes; WorldForge's `LightEntry` + the community
`DBCTemplate.bt` `LightRec` + wowdev.wiki all agree on fields 0–6):

| Field | Name | Type | Notes |
|-------|------|------|-------|
| 0 | ID | u32 | |
| 1 | ContinentID (MapID) | u32 | which map this light applies to |
| 2 | X | f32 | world-space game coords (Z-up). See §5 on the WoWMapViewer /36 quirk. |
| 3 | Y | f32 | |
| 4 | Z | f32 | |
| 5 | FalloffStart (inner radius) | f32 | full strength within this radius |
| 6 | FalloffEnd (outer radius) | f32 | zero strength beyond; blend between |
| 7..N | LightParamsID[ ] | u32[] | refs into LightParams.dbc, one per weather/condition |

The **default/global** light for a map is the record at **X=Y=Z=0** (continent
default, infinite falloff). Specific zones add positioned lights that blend in by
distance. `LightParamsID[0]` = clear-weather/standard; that is the only one you
need for a clear-daylight offline render. (Underwater=1, stormy=2, etc.)

**Param-count conflict (resolve at runtime, see §5):** WorldForge's `dbc_defs`
asserts vanilla has **5** params (fields 7–11, 12 fields total). wowdev.wiki,
WoWFormatLib, and `DBCTemplate.bt` say **8** params (`m_lightParamsID[8]`, 14
fields total). Do **not** hardcode either — read `dbc.fieldCount()` and compute
`numParams = fieldCount - 7`. Only param[0] is needed regardless.

### 2.5 LightParams.dbc

LightParams is the indirection between a Light and its color/float curves. Its
own `ID` is what the band tables key off. Vanilla fields (wowdev.wiki):
`ID, highlightSky(bool), lightSkyboxID(ref), glow(f32), waterShallowAlpha,
waterDeepAlpha, oceanShallowAlpha, oceanDeepAlpha` (+ flags in later builds).
The **water/ocean shallow/deep alpha** here is exactly what tints + sets the
transparency floor of your liquid surface — pair it with the per-vertex MCLQ
depth.

### 2.6 LightIntBand.dbc — 18 color curves per LightParams

Layout per record: `ID, num (count, 0..16), time[16] (u32, 0..2880 half-minutes
of a day), values[16] (u32 BGRA/0xRRGGBB packed colors)`.

**FirstId formula:** the 18 bands for a given `LightParams.ID` are 18 consecutive
records. WoWMapViewer (`sky.cpp`) uses `FirstId = LightParamsID * 18` and reads
`FirstId + i` for i in 0..17 (its records are 0-indexed). wowdev's community
phrasing is `ID*18 - 17` (1-indexed). **Same rows** — pick the form that matches
how your `Dbc` reader indexes (0-based `rec` ⇒ `(paramId-1)*18 + i`, since DBC
IDs are 1-based but records are 0-based; verify the first LightParams ID in the
real file and the band ID column, don't assume contiguity — index by the `ID`
column).

**The 18 band indices (from WoWMapViewer `sky.h` `SkyColorNames` enum — the
canonical mapping, corroborated by the LIT-file 18 color tracks):**

| Idx | Constant | Use |
|----:|----------|-----|
| 0 | LIGHT_GLOBAL_DIFFUSE | **directional/sun diffuse color** → scene diffuse |
| 1 | LIGHT_GLOBAL_AMBIENT | **ambient color** → scene ambient |
| 2 | SKY_COLOR_0 | sky top |
| 3 | SKY_COLOR_1 | sky middle |
| 4 | SKY_COLOR_2 | middle→horizon |
| 5 | SKY_COLOR_3 | above horizon |
| 6 | SKY_COLOR_4 | horizon |
| 7 | FOG_COLOR | **fog / distant mountains** → scene fog color |
| 8 | (unknown) | |
| 9 | SUN_COLOR | sun disc |
| 10 | SUN_HALO_COLOR | sun halo |
| 11 | (unknown) | |
| 12 | CLOUD_COLOR | clouds |
| 13–14 | (unknown) | |
| 15 | WATER_COLOR_DARK | **deep-water tint** → liquid color |
| 16 | WATER_COLOR_LIGHT | **shallow-water tint** → liquid color |
| 17 | SHADOW_COLOR | terrain shadow tint |

So liquid color (15/16), fog (7), ambient (1) and diffuse (0) all come from the
*same* lookup — lighting and water are genuinely one feature.

### 2.7 LightFloatBand.dbc — 6 float curves per LightParams

Same record shape (`ID, num, time[16], values[16]` but values are f32). Band
meanings (wowdev community / LIT docs):

| Idx | Use |
|----:|-----|
| 0 | **Fog end distance** (the raw value × 36 = yards at which world is fully fogged) |
| 1 | **Fog start multiplier** (0..1; fogStart = fogEnd × this) |
| 2 | Celestial glow-through (sun/moon brightness through clouds) |
| 3 | Cloud density (0..1) |
| 4–5 | unknown |

`FirstId = LightParamsID * 6` (or `ID*6 - 5` 1-indexed). For offline stills you
need bands 0 and 1 to set a depth-fog `[start,end]` for the fog blend.

### 2.8 Time-of-day interpolation

Both band tables store curves over a **2880-tick day** (0=midnight, 1440=noon).
`colorFor(t)` = linear interpolation between the two surrounding
`time[]` keyframes, with wraparound (`if t < time[0] t += 2880`). For a fixed
"noon" render, sample at t=1440. (WoWMapViewer `sky.cpp` `colorFor()`.)

---

## 3. How other engines implement it

### Liquid
* **Noggit3** (`src/noggit/MapHeaders.h`, `src/noggit/liquid_layer.cpp`):
  parses the `mclq` struct above into a `liquid_layer`; per vertex stores
  `position`, `uv`, and a normalized `depth` (`uint8/255`). `update_indices()`
  walks the 8×8 tiles, skipping `dont_render`, emitting 2 triangles per rendered
  cell over the 9×9 grid (indices `offset+p`, `offset+p+9`, `offset+p+9+1`).
  `update_vertex_opacity()` computes shoreline alpha from
  `clamp((waterZ - terrainZ + 1) * factor, 0, 1)`. Magma UV from
  `magma.x/256 * 3.0`. Renders as one translucent quad layer per liquid type with
  the LiquidType texture.
* **WoWMapViewer / WoWModelViewer** (older `SWVert`/`SLVert`/`SMLiquidVert`
  union): same union; `SWVert` uses `depth` (index into a precalc depth table),
  magma uses `s,t` × `MAGMA_SCALE_ADT = 3.0/256.0`.
* **wow.export / WoWFormatLib** (`ADTReader.cs`): *skips* MCLQ
  (`case MCLQ: continue;`) — it only renders the modern MH2O path, so it is **not**
  a useful reference for vanilla MCLQ. (Useful counter-example: don't copy
  wow.export here.)
* **wow-adt (Rust crate)**: `MclqChunk { min_height, max_height,
  vertices[81], tile_flags[64], liquid_type }` with `relative_depth()` /
  `base_height()` — confirms the same 720-byte core.

### Lighting
* **WoWMapViewer (cleverca22 fork) `src/sky.cpp` + `sky.h`** — the reference
  vanilla implementation: `Sky` reads each Light.dbc record (Map, PositionX/Y/Z,
  RadiusInner/Outer, DataIDs), builds 18 `SkyColor` curves via
  `FirstId = DataIDs*18`, and `findSkyWeights()` blends active skies by distance
  (`<r1` exclusive weight 1; between `r1..r2` linear; `>r2` none).
  `SkyColorNames` enum = the 18-band table in §2.6. `colorFor(t)` does the
  2880-tick interpolation. This is the single best model to mirror.
* **cmangos/mangos-zero**: load **none** of the light tables (server doesn't
  light the sky) — confirms client-only. mangos-zero's docs do enumerate the
  same field layout though.
* **wow.export** applies the modern (WotLK+) Light/LightData path with skyboxes;
  the *18-band semantics* still apply but the table is merged into LightData in
  later builds — not vanilla.

---

## 4. Implementation plan for WorldForge (smallest-first)

Existing anchors: `src/terrain.hpp/.cpp` (MCLQ parse + mesh build),
`src/scene.hpp/.cpp` (`Scene`, `renderScene`, `lightDir`),
`src/raster.cpp` (`rasterMesh`, `rasterTexMesh` w/ `alphaBlend`),
`src/dbc_defs.hpp/.cpp` (`LightEntry`, `lightEntry`), `src/coords.hpp`
(world placement), `src/wow_files.hpp` (`Dbc`).

**Step 1 — Liquid surface mesh (no lighting dep). [smallest]**
* In `terrain.cpp` `parseMclq`, *stop discarding* the per-vertex union: keep
  `depth[81]` (water) or raw `s,t` (magma). Add `std::array<uint8_t,81> depth{}`
  and optionally `std::array<uint8_t,64> tileType{}` to `MclqLayer`
  (`terrain.hpp`). Keep the existing 720-byte bound.
* Add `Mesh buildLiquidMesh(const MapChunk& mc, int blockX, int blockY)` in
  `terrain.cpp` next to `buildChunkMesh`: reuse the **same 9×9 outer-grid world
  XY** that `buildChunkMesh` computes, substitute Z = `liquid.heights[i]`
  (clamped to min/max), and for each of the 64 tiles emit 2 triangles only when
  `liquidTileRenders(renderFlags[t])`. Normal = +Z (flat). This is a normal
  `Mesh`; renders immediately with `rasterMesh` for a first sanity pass
  (flat-blue water).
* **Validate:** render a known watery tile (Elwynn river, or Westfall coast)
  with the liquid mesh in a contrasting flat color and eyeball that water fills
  the riverbed/sea exactly where the client shows it, with correct height.

**Step 2 — Translucent + depth-faded water.**
* Add a `TexMesh`/per-vertex-alpha path: compute per-vertex shoreline opacity
  `clamp((waterZ - terrainZ + 1) * k, 0, 1)` (terrainZ = nearest MCVT height),
  combine with `depth/255`. Render via `rasterTexMesh(..., alphaBlend=true)`
  (already exists; translucent surfaces don't write depth). Color = a per-type
  base (water blue / magma orange / slime green) until Step 4 supplies real
  tints. Magma/slime render opaque (alpha 1, emissive-ish — just don't darken by
  light).
* **Validate:** water should fade to transparent at banks and read solid in deep
  areas — compare against client screenshots of the same river bend.

**Step 3 — Light.dbc band tables + lookup. [the lighting half]**
* New `src/lighting.hpp/.cpp` (or extend `dbc_defs`): add
  `LightParamsEntry`, and raw band readers `lightIntBand(dbc, rec)` →
  `{num, time[16], color[16]}`, `lightFloatBand(dbc, rec)` similarly.
  **Read counts from `dbc.fieldCount()`**; do not hardcode 12-vs-14 fields.
* Build an index: for the current map, collect `Light` records, separate the
  global (pos=0) from positioned ones; for each, resolve `LightParamsID[0]` →
  LightParams.ID → 18 IntBands (`(paramId-?)*18+i`, indexed by the band `ID`
  column, not by record offset) + 6 FloatBands.
* `LightingSample lightingAt(worldPos, float dayTick=1440)`:
  1. `findSkyWeights`: weight each positioned light by distance vs
     `falloffStart/End`; global light fills the remainder.
  2. For each band, `colorFor(dayTick)` (2880-tick lerp), blend by weight.
  3. Return `{ ambient(1), diffuse(0), fog(7), fogStart/End(float 1,0×36),
     waterDark(15), waterLight(16), shadow(17) }`.
* **Validate offline:** dump `lightingAt()` for Elwynn vs Durotar vs Tirisfal at
  noon and confirm the ambient/fog hues differ in the expected direction
  (green-blue Elwynn, red-brown Durotar, sickly-green Tirisfal). Print the raw
  packed colors and eyeball vs client.

**Step 4 — Feed lighting into the scene/shader.**
* Extend `Scene` (`scene.hpp`): add `Vec3 ambient, diffuse, fogColor;
  float fogStart, fogEnd; Vec3 waterDark, waterLight;` populated from
  `lightingAt(cameraOrTileCenterWorldPos)`. Keep `lightDir` (sun direction is
  not in the bands — derive from time-of-day or keep the current default; the
  bands give sun *color*, not direction).
* In `raster.cpp` shading: replace the hardcoded ambient term with `Scene.ambient`
  and modulate diffuse by `Scene.diffuse`; apply a depth fog blend toward
  `fogColor` over `[fogStart,fogEnd]` in `rasterMesh`/`rasterTexMesh`.
* For liquid: tint the Step-2 water by `lerp(waterLight, waterDark, depthFactor)`
  instead of the placeholder blue.
* **Validate:** full tile render of the watery zone vs a real 1.12.1 client
  screenshot at matched camera/time — fog distance, ambient tint, and water
  color should visually agree.

**Validation harness throughout:** reuse the existing `terrain_demo` /
`scene_demo` offline render path; add a CLI that loads a specific ADT + the
client's `Light.dbc`/`LightParams.dbc`/`LightIntBand.dbc`/`LightFloatBand.dbc`
from the real MPQ (via `src/mpq`, `src/client_data`) and writes a PNG. Compare
PNG to a captured client screenshot of the same coordinates.

---

## 5. Risks / pitfalls / open questions

1. **Light.dbc param-count conflict (highest risk).** WorldForge's `dbc_defs.hpp`
   comment claims vanilla Light.dbc = **12 fields / 5 params** ("verified vs the
   real dbc"); wowdev.wiki, WoWFormatLib, and `DBCTemplate.bt` all say **14
   fields / 8 params**. One is wrong for 5875. **Resolve empirically offline:**
   parse the real client `Light.dbc` and print `fieldCount()` — it will be 12 or
   14, settling it. Code must use `fieldCount()` and never assume. (Only param[0]
   is needed for clear-daylight, so this doesn't block Steps 1–2.)
2. **Coordinate units / the ×36 quirk.** WoWMapViewer divides Light positions by
   **36** and multiplies LightFloatBand fog distance by **36**. The /36 on
   *position* is a known older-build oddity; in retail-era 1.12 the Light X/Y/Z
   are normal world coords (Z-up). The ×36 on *fog distance* (LightFloatBand band
   0) is real and required. Verify the position scale against a known light's
   coordinates in the real client before trusting either; cross-check by picking
   the global (pos=0) light first, which is scale-independent.
3. **Band indexing is ID-keyed, not offset-keyed.** DBC IDs are 1-based but
   `Dbc` records are 0-based, and band IDs are *not guaranteed contiguous from 1*.
   Build a `map<ID, recIndex>` and look up by the `ID` column; the `×18`/`×6`
   formula gives the *first ID*, then read by ID. Off-by-one here silently
   produces wrong colors (e.g. fog where ambient should be).
4. **`Dbc` has no float accessor.** `dbc_defs.cpp` already uses a local
   `fieldF32` (bit-reinterpret `getU32`). LightFloatBand needs the same; expose a
   shared `fieldF32` rather than duplicating.
5. **Color packing.** IntBand values are packed colors — vanilla is typically
   **0x00RRGGBB** (BGRA in memory on LE). Confirm channel order against a known
   value (e.g. a clearly-blue sky-top band) before wiring into the shader; a
   swapped R/B turns blue skies orange.
6. **MCLQ vs MH2O.** Vanilla 1.12.1 is **MCLQ only** (MH2O arrives in WotLK).
   Don't add MH2O. wow.export/WoWFormatLib skip MCLQ entirely — not a model for
   us. WorldForge's existing single-layer `MclqLayer` is right for vanilla (MH2O
   can stack multiple layers; MCLQ is one per MCNK selected by the header flag).
7. **Multiple liquid flags / sub-tile type.** The MCNK header can in principle
   carry the wrong flag combo; trust `mclq_tile.liquid_type:3` per-tile if it
   disagrees with the header. For rendering color, header flag is sufficient in
   1.12.
8. **Sun direction not in the data.** The bands give sun/diffuse *color*, not the
   light *direction*. Keep deriving `lightDir` from time-of-day (or the current
   constant) — the LightDB position is the sky-zone center, not a sun vector.
9. **Flow/animation ignored.** `n_flowvs`/`flowvs` and per-vertex flow bytes are
   parsed-past for now; fine for stills, needed later for animated water.
10. **Performance/altitude.** All of this is offline software-rendered — clarity
    over speed; a per-tile-center single `lightingAt` sample (not per-pixel) is
    plenty for validation.

---

### Source citations
* wowdev.wiki **ADT/v18** (MCLQ, MCNK liquid flags, ofsMCLQ@0x60) and
  **DB/Light**, **DB/LightIntBand**, **DB/LightFloatBand**, **DB/LightParams**.
* **Noggit3** `src/noggit/MapHeaders.h` (`mclq`/`mclq_vertex`/`mclq_tile`
  verbatim) and `src/noggit/liquid_layer.cpp` (depth normalize, shoreline
  opacity, index gen).
* **WoWMapViewer/WoWModelViewer** (cleverca22/wowmapviewer) `src/sky.cpp`
  (FirstId=DataIDs*18, `colorFor` 2880-tick lerp, `findSkyWeights`) and
  `src/sky.h` (`SkyColorNames` 18-band enum). `SWVert`/`SLVert` magma scale
  `MAGMA_SCALE_ADT = 3.0/256.0`.
* **cmangos/mangos-classic** `DBCStructure.h` (`LiquidTypeEntry`),
  `DBCStores.cpp` (no Light tables loaded → client-only).
* **WoWFormatLib** `ADTReader.cs` (MCLQ skipped — counter-example) ;
  **wow-adt** Rust crate `MclqChunk`; community `wow010/DBCTemplate.bt`
  `LightRec` (`m_lightParamsID[8]`).
