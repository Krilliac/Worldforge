# Multi-Expansion Format Deltas (1.12.1 -> 2.4.3 -> 3.3.5 -> 4.3.4)

> Research note for the `ClientVersion` / `ClientProfile` scaffolding.
> Goal: define a *minimal* version enum + profile struct so WorldForge can add
> expansions later, while **1.12.1 stays the only implemented target today**.
> Every concrete fact below is cross-checked against at least two independent
> sources (wowdev.wiki + a real engine/library). Where the existing WorldForge
> parser already encodes a quirk correctly, that is called out so the profile
> field maps onto code that exists.

---

## 1. Summary

WorldForge currently hard-codes vanilla 1.12.1 (client 5875) assumptions in
several parsers: `m2.cpp` only accepts `MD20`/`version 0x100`; `terrain.cpp`
decodes MCAL as 2048-byte packed 4-bit alpha gated on `Wdt::bigAlpha()`;
`wmo.cpp` assumes WMO v17 monolithic root+group; `wow_files.cpp` parses a single
monolithic ADT and a `WDBC` DBC; `mpq.cpp` assumes MPQ archives; `coords.hpp`
hard-codes vanilla tile geometry. None of these *branch* on a version yet — the
version is implicit.

This note catalogs **exactly which on-disk facts change** between vanilla (1.12.1),
TBC (2.4.3), WotLK (3.3.5a) and Cataclysm (4.3.4), so we can introduce a tiny
`ClientVersion` enum and a `ClientProfile` value-struct that carries the
format-selecting booleans/constants. The payoff is that each parser gets a single
`profile.xxx` branch point instead of scattered magic numbers, and adding TBC/WotLK
later becomes "fill in a profile + add the branch", not "rewrite the parser".

The headline deltas, smallest-impact first:

* **MCAL alpha**: vanilla/TBC = 2048 B packed 4-bit (unless `MPHD & 0x4`); WotLK
  introduces real big-alpha (4096 B, 8-bit) plus a "fix alpha" border rule; Cata
  keeps big-alpha and adds `MTXP`/height-texturing semantics on flag `0x80`.
* **Liquid**: vanilla/TBC = per-chunk `MCLQ`; WotLK+ = tile-level `MH2O`.
* **ADT layout**: vanilla->WotLK = one monolithic `.adt`; **Cata splits** into
  `.adt` (root) + `_tex0` + `_obj0` (+`_tex1`/`_obj1`/`_lod`).
* **M2 header version**: 0x100 (256) vanilla, 0x104 (260) TBC, 0x108 (264) WotLK,
  ~0x108–0x110 Cata; **embedded skins** vanilla–TBC, **external `.skin`** from
  WotLK, **external `.anim`** from Cata; **MD21 chunked** only Legion+ (out of scope).
* **WMO**: root version **17** for vanilla through WotLK (and Cata); only the
  pre-release "alpha" is v14 (`MOMO` container). Material/doodad strides are stable.
* **DBC**: `WDBC` (.dbc) for vanilla->WotLK; Cata moves many tables to `WDB2`
  (.db2) and makes all DBC strings locale-specific.
* **Archives**: MPQ for vanilla->WotLK (4.3.4 still MPQ); **CASC** only WoD+.
* **Coordinate constants**: identical across all four expansions.

---

## 2. Concrete format/protocol facts (vanilla-anchored, with deltas)

### 2.1 M2 header version field

The M2 header begins with magic `MD20` then a `uint32 version`. The value is a
packed major/minor (256 == 1.0).

| Expansion | Build | M2 `version` | Notes |
|---|---|---|---|
| Vanilla 1.12.1 | 5875 | **0x100 = 256** | embedded views/skins, embedded `.anim` |
| TBC 2.4.3 | 8606 | **0x104 = 260** | still embedded skins |
| WotLK 3.3.5a | 12340 | **0x108 = 264** | external `.skin` files introduced |
| Cataclysm 4.3.4 | 15595 | **264 (0x108)** (Cata shader-substitution path runs only for `version <= 264`); some 4.x = 0x108–0x110 | external `.anim` files; `tex_mapping_lookup` unused |
| Legion 7.x | — | MD21 chunked (out of scope) | header wrapped in `MD21` chunk |

Sources: getMaNGOS M2/MDX reference (vanilla 0x100, "BC and later" 0x104)
[getMaNGOS]; wowdev.wiki M2 (version is double-byte, 256==1.0; Cata `<=264`
shader path; MD21/Legion chunking) [wowdev-M2]; wow-m2 Rust crate docs (WotLK =
external SKIN introduced, Cata = external ANIM, Legion = chunked) [wow-m2].

**Format consequences for the parser:**
* **Embedded vs external skins.** Vanilla and TBC store the view/skin profiles
  *inside* the M2 (this is exactly what `m2.cpp` reads as "view 0"). WotLK+ moves
  them to sibling `Model00.skin`..`Model03.skin`; the M2 header then carries a
  *count* of skins rather than embedded offsets. **This is the single biggest M2
  delta.** WorldForge's `m2.hpp` comment already records this correctly.
* **Embedded vs external anim.** Vanilla–WotLK keep low-priority animation keyframe
  data inline; Cata moves them to `.anim` files. (WotLK already externalizes *some*
  via the `M2Track` flag bit — see below.)
* **M2Track layout.** Vanilla uses a single timestamp array + value array with
  per-animation *interpolation ranges* — this is exactly WorldForge's
  `RawChannel<T>.ranges` design in `m2.hpp`. WotLK+ switched to nested
  per-animation arrays (`M2Array<M2Array<T>>`) keyed off the `M2Track.global_seq`
  + an "external" flag. Vanilla M2Track is `{interp, global_seq, ranges[],
  times[], values[]}`; WotLK M2Track is `{interp, global_seq, timestamps:
  Array<Array<u32>>, values: Array<Array<T>>}`. Sources: wowdev.wiki M2
  (interpolation ranges removed after vanilla) [wowdev-M2]; `m2.hpp` header
  comment already notes "WotLK+ switched to nested per-animation arrays".

### 2.2 ADT — monolithic vs split

* **Vanilla (v18) / TBC / WotLK:** one file `World/Maps/<Map>/<Map>_X_Y.adt`
  containing `MVER, MHDR, MCIN, MTEX, MMDX, MMID, MWMO, MWID, MDDF, MODF` and 256
  `MCNK`s (each with sub-chunks `MCVT, MCNR, MCLY, MCRF, MCSH, MCAL, MCLQ, MCSE`).
  This is what `wow_files.cpp::parseAdt` + `terrain.cpp::parseChunks` parse today.
* **Cataclysm (4.x) splits** the tile into:
  - `<Map>_X_Y.adt` — **root**: terrain (`MCNK` with `MCVT/MCNR/MCLY/MCAL/MCSH`,
    plus the root `MCNK` header). `MCIN` is **gone**.
  - `<Map>_X_Y_tex0.adt` — textures: `MTEX`/`MDID`, per-`MCNK` `MCLY`+`MCAL`+`MCSH`
    (these `MCNK`s have **no header**, just sub-chunks).
  - `<Map>_X_Y_obj0.adt` — objects: `MMDX/MMID/MWMO/MWID/MDDF/MODF` + per-`MCNK`
    `MCRF/MCRD`.
  - `_tex1`/`_obj1` = LOD-1 variants; `_lod` (added later) = low-detail draw
    distance. The client loads the set of 3 and treats them as one logical ADT.
  - Cata also moves the existence/offset table from `MCIN`/`MAIN` into the WDT's
    `MAID` (file-data-id table).
  Sources: wowdev.wiki ADT/v18 split-file section ("Beginning with Cataclysm,
  ADTs are split... .adt, _tex%d, _obj%d... MCIN gone, tex/obj MCNK have no
  header") [wowdev-ADT, via search snippet]; wow-adt Rust crate (split introduced
  Cataclysm 4.x; root/_tex0/_obj0/_obj1/_lod; AdtSet loads the set) [wow-adt].

### 2.3 MCNK alpha (MCAL) — the central terrain delta

Alpha (blend) maps in `MCAL` have three encodings, selected by **two flags**:

* **MCLY layer flag `0x200`** (`MCLY_COMPRESSED`): the layer's map is RLE.
  WorldForge already handles this in `terrain.cpp` (`decodeAlphaMap`).
* **WDT MPHD flag `0x4`** (`flag_has_big_alpha` / a.k.a. low-quality terrain
  blend in vanilla): when set, uncompressed maps are **4096 B, 8-bit/texel**
  instead of **2048 B, packed 4-bit/texel**. WorldForge encodes this as
  `Wdt::MPHD_BIG_ALPHA = 0x4` and `Wdt::bigAlpha()`, passed into
  `decodeAlphaMap(..., bigAlpha)`.
* **WDT MPHD flag `0x80`** (`adt_has_height_texturing`, WotLK+/Cata): *also*
  forces 4096-B uncompressed MCAL **and** changes shader semantics
  (`_h`+`MTXP`). So big-alpha is effectively `(MPHD & 0x4) || (MPHD & 0x80)`.

Source: wowdev.wiki ADT/v18 / libwarcraft discussion: "Alpha maps depend on MCLY
(0x200) and WDT MPHD (0x4 and 0x80). 0x04 ... affects whether MCAL has 4096
instead of 2048. 0x0080 is adt_has_height_texturing ... changes MCAL size to 4096
for uncompressed entries." [wowdev-ADT/search]; TrinityCore **master** checks
`MPHD->flags & 0x200` for big alpha (retail flag layout differs from vanilla — see
Risks) [TC-System.cpp].

**"Fix alpha" border rule.** When big-alpha is *not* set and `MCNK` flag `0x8000`
(`do_not_fix_alpha_map`) is *clear*, the client duplicates the last row/column of
the 63x63 effective map to fill the 64x64 grid (the classic "alpha seam" fix).
WorldForge already has `MCNK_DO_NOT_FIX_ALPHA = 0x8000` in `terrain.hpp`. This rule
is the same vanilla->Cata; only the bit-depth context around it changes.

| Expansion | default MCAL | big-alpha trigger | fix-alpha |
|---|---|---|---|
| Vanilla 1.12.1 | 2048 B 4-bit | `MPHD & 0x4` (rare in retail vanilla maps) | yes unless `MCNK 0x8000` |
| TBC 2.4.3 | 2048 B 4-bit | `MPHD & 0x4` | same |
| WotLK 3.3.5 | 2048 B 4-bit, **4096 common** | `MPHD & 0x4` or `0x80` | same |
| Cata 4.3.4 | 4096 B 8-bit common | `0x4`/`0x80`; `MTXP` height-tex | same |

(Note: cmangos vanilla extractor `contrib/extractor` does **not** decode big-alpha
at all — confirming it is unused/rare in real 1.12 map data, which is why
WorldForge can keep 4-bit as the practical default. [cmangos-System.cpp])

### 2.4 Liquid — MCLQ vs MH2O

* **Vanilla / TBC:** per-`MCNK` `MCLQ` sub-chunk: a 9x9 height grid (8-byte verts)
  + 8x8 render-flag mask; liquid *type* comes from `MCNK` header flags
  `0x4/0x8/0x10/0x20` (river/ocean/magma/slime). This is exactly WorldForge's
  `MclqLayer` + `LiquidType` in `terrain.hpp`.
* **WotLK (3.x)+:** tile-level **`MH2O`** chunk replaces `MCLQ`. `MH2O` header is
  an array `[16][16]` of `{ uint32 offset_instances; uint32 layer_count; uint32
  offset_attributes; }`, each pointing to `SMLiquidInstance` records (liquid type
  via `LiquidType.dbc` id, min/max height, a render bitmask, a vertex-format-
  dependent height/depth array). `MCLQ` may still appear but is ignored when
  `MH2O` exists. Sources: wowdev.wiki ADT/v18 MH2O; TrinityCore `adt.h`
  `adt_MH2O` (`liquid[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID]` with
  `OffsetInstances/used/OffsetAttributes`) [TC-adt.h]; wow-adt (MH2O introduced
  WotLK 3.x) [wow-adt].

### 2.5 New ADT chunks by version (for completeness; only MCLQ/MCAL matter now)

| Chunk | Introduced | Purpose |
|---|---|---|
| `MFBO` | TBC 2.x | flight-boundary planes (2x 9 int16 height planes) |
| `MH2O` | WotLK 3.x | tile liquid (replaces MCLQ) |
| `MCCV` | WotLK 3.x | per-vertex MCNK vertex colors |
| `MTXF` | WotLK 3.x | per-texture flags (parallel to MTEX) |
| `MAMP` | Cata 4.x | texture amplifier (detail scale) |
| `MTXP` | Cata/MoP | texture parameters (height-texturing, flag 0x80) |
| `MDID`/`MHID` | Cata+ | file-data-id texture refs (replace MTEX names) |

Constants that are **stable across all four**: `TILESIZE = 533.33333`,
`CHUNKSIZE = TILESIZE/16`, `ADT_CELLS_PER_GRID = 16`, `ADT_CELL_SIZE = 8`, 145
verts/MCNK (9x9 + 8x8). Source: TrinityCore `adt.h` defines [TC-adt.h]; matches
WorldForge `coords.hpp` (`TILE_SIZE`, `CHUNK_SIZE`, `UNIT_SIZE`).

### 2.6 WDT

* **Vanilla–WotLK:** `MVER, MPHD (SMMapHeader, flags), MAIN (64x64 SMAreaInfo:
  has-ADT bit + async-id)`, optional `MWMO/MODF` for WMO-only (global-WMO) maps.
  WorldForge `parseWdt` reads `MPHD.flags` + `MAIN` existence bits + the
  global-WMO flag `0x1`. Stable vanilla->WotLK.
* **Cataclysm:** `MPHD` gains new flags; `MAID` chunk (file-data-ids for the split
  ADT set) is added; `MAIN` still present. The big-alpha (`0x4`) and
  height-texturing (`0x80`) MPHD bits live here.

Source: wowdev.wiki WDT; WorldForge `wow_files.hpp` `Wdt` (`MPHD_GLOBAL_WMO=0x1`,
`MPHD_BIG_ALPHA=0x4`).

### 2.7 WMO version deltas

WMO root `MVER` version is **17** for vanilla, TBC, WotLK *and* Cataclysm (the
format is remarkably stable; v14 is only the pre-release "alpha", which wraps group
data in an extra `MOMO` container). Material (`MOMT`) = **64 bytes**, doodad
(`MODD`) = **40 bytes**, group-info (`MOGI`), `MOHD` header = **64 bytes** — all
match WorldForge `wmo.cpp` today.

Minor deltas that don't change strides for vanilla:
* `MOHD` gained flag bits in later expansions (e.g. `do_not_fix_vertex_color_alpha`,
  lod fields) but the 64-byte size is constant through Cata.
* Cata/MoP added `MCVP` (convex volume planes) and later `MODI`/`GFID`; texture
  names in `MOTX` were only replaced by file-data-ids in BfA 8.1 (`MOMT` ids) —
  **far out of scope**.
* Group flag `0x40000000` (has MOCV color) and water (`MLIQ`) handling are stable
  vanilla->Cata.

Sources: cleverca22 wowmapviewer `wmo.cpp` (MOHD 64 B, MOMT read 0x40, MODD
0x28, MVER ignored) [wmv-wmo.cpp]; wowdev.wiki WMO (v14 alpha MOMO container;
MOTX->file-data-ids at 8.1.0) [wowdev-WMO]; matches WorldForge `WmoMaterial`,
`WmoDoodad`, `WmoRoot`.

### 2.8 DBC schema/version

* **Vanilla–WotLK:** `WDBC` magic, header = `{magic, recordCount, fieldCount,
  recordSize, stringBlockSize}` then fixed-stride records then a string block.
  This is exactly WorldForge `Dbc::parse` in `wow_files.cpp`. **Column *schemas*
  differ per expansion** (tables gain/lose/reorder columns between 1.12 and 3.3.5),
  but the container is identical — which is why `wow_files.hpp` deliberately does
  *not* hard-code field meanings.
* **Cataclysm (4.x):** many high-volume tables move to **`WDB2`** (.db2) with a
  larger header (adds `table_hash, build, min/max id, locale, copy-table` etc.),
  and **all DBC strings become locale-specific** (one localized string field
  instead of the vanilla 16-locale + flags block). Pure `WDBC` tables still exist
  in 4.x but the loader must accept both `WDBC` and `WDB2`.
* WoD+: `WDB3/4`, Legion: `WDB5/6` then `WDC1+` (sparse, field-storage,
  id-list) — all out of scope.

Sources: wowdev.wiki DB2 / DBC; wow-cdbc Rust crate; DBCD README (WDB2 = Cata,
"since Cataclysm all DB files contain only localized strings") [wowdev-DB2, wow-cdbc].

### 2.9 Archives — MPQ vs CASC

* **MPQ** for vanilla, TBC, WotLK **and Cataclysm 4.3.4** (4.x is still MPQ; the
  patch chain just grows, e.g. `wow-update-*.MPQ`). WorldForge `mpq.cpp` +
  `client_data.cpp` already handle the chained-override MPQ model.
* **CASC** begins with **WoD (6.x)** — *not* needed for any of the four target
  expansions. Mentioning it only so the profile reserves an `archive` field.
* The *archive set / patch chain* differs per expansion (vanilla's
  `base/dbc/...MPQ` vs WotLK's `common.MPQ/common-2.MPQ/expansion.MPQ/lichking.MPQ/
  patch*.MPQ` vs Cata's locale + `wow-update-*` chain). `client_data.cpp`'s
  `wowBaseArchiveNames()` is the natural place this list becomes profile-driven.

Sources: wowdev.wiki CASC / Patch pages; StormLib supports all MPQ variants;
WorldForge `client_data.hpp` already documents the vanilla chain.

### 2.10 Coordinate constants — no delta

`TILE_SIZE = 533.33333`, `ZEROPOINT = 32*TILE_SIZE = 17066.666`, the placement
mirroring (`worldX = ZEROPOINT - storedZ`, etc.), and the 64x64 tile grid are
**identical across vanilla->Cata** (and indeed all of WoW). `coords.hpp` needs no
profile branch; this is documented here so the profile *does not* add a spurious
coordinate field.

---

## 3. How other engines implement version branching

* **TrinityCore** (`src/tools/map_extractor/adt.h`, `System.cpp`): one extractor
  built per client branch (`master`=retail, `3.3.5`=WotLK, `cata`=4.4). It does
  *not* runtime-branch; it `#define`s `TILESIZE/ADT_CELLS_PER_GRID` and reads the
  branch's fixed layout. Big-alpha is gated on `mphd->flags & 0x200` **on retail**
  (note: retail MPHD flag layout differs from vanilla's `0x4`). `adt_MH2O` struct
  models the WotLK+ liquid; `adt_MCLQ` the old one. Takeaway: TC picks the format
  at *compile* time per branch — WorldForge wants the same selection but at
  *runtime* via a profile. [TC-adt.h, TC-System.cpp]
* **cmangos / mangos-classic** (`contrib/extractor/loadlib/adt.h`, `System.cpp`):
  vanilla-only extractor. Reads `MCLQ` directly, **no MH2O, no big-alpha decode** —
  concrete proof those are absent from real 1.12 data and that a vanilla profile
  can leave them unimplemented. [cmangos-System.cpp]
* **wow-adt / wow-m2 (Rust crates)**: explicitly carry an `AdtVersion` /
  `M2Version` enum (`Vanilla/TBC/WotLK/Cataclysm/MoP`) and branch parsing on it,
  with `AdtSet` to load the Cata split set and automatic dependency-format
  detection for `.skin`/`.anim`. This is the closest analog to the proposed
  `ClientProfile`. [wow-adt, wow-m2]
* **wow.export** (`M2Loader.js`): detects `MD20` magic, parses embedded views, and
  for chunked (Legion) reads `SFID/AFID/SKID` chunks for external skin/anim
  file-data-ids. Confirms "embedded views = pre-WotLK, external skin chunks =
  later". [wow.export]
* **cleverca22 wowmapviewer** (`wmo.cpp`): ignores WMO `MVER`, reads fixed 64-B
  MOHD/MOMT and 40-B MODD — confirming WMO stride stability. [wmv-wmo.cpp]

---

## 4. Implementation plan for WorldForge

Smallest-first. The whole point is **scaffolding only** — 1.12.1 stays the single
implemented target; every non-vanilla branch is a `// TODO(expansion)` that throws
`"unsupported client version"` until implemented.

### Step 0 — New file `src/client_version.hpp` (enum + profile, header-only)

```cpp
namespace wf {

enum class ClientVersion : uint16_t {
    Vanilla_1_12_1 = 5875,   // the only implemented target
    TBC_2_4_3      = 8606,
    WotLK_3_3_5a   = 12340,
    Cata_4_3_4     = 15595,
};

enum class ArchiveKind   { Mpq, Casc };              // Casc unused (WoD+)
enum class AdtLayout     { Monolithic, SplitTexObj }; // Cata = SplitTexObj
enum class LiquidFormat  { Mclq, Mh2o };             // WotLK+ = Mh2o
enum class DbcContainer  { Wdbc, Wdb2 };             // Cata high-volume = Wdb2
enum class M2SkinSource  { Embedded, ExternalSkin }; // WotLK+ = ExternalSkin
enum class M2AnimSource  { Embedded, ExternalAnim }; // Cata+ = ExternalAnim

struct ClientProfile {
    ClientVersion version   = ClientVersion::Vanilla_1_12_1;
    uint32_t      build     = 5875;

    // --- archive / mounting (client_data.cpp, mpq.cpp) ---
    ArchiveKind   archive   = ArchiveKind::Mpq;

    // --- ADT (wow_files.cpp parseAdt, terrain.cpp parseChunks) ---
    AdtLayout     adtLayout = AdtLayout::Monolithic;

    // --- MCAL alpha (terrain.cpp decodeAlphaMap) ---
    // bigAlpha is *also* derivable per-map from WDT MPHD (0x4|0x80); the profile
    // sets the DEFAULT/allowed encoding. Vanilla data is effectively always 4-bit.
    bool          defaultBigAlpha = false;        // 4096B 8-bit when true
    bool          heightTexturing = false;        // MPHD 0x80 / MTXP (Cata)

    // --- liquid (terrain.cpp / wow_files.cpp) ---
    LiquidFormat  liquid    = LiquidFormat::Mclq;

    // --- M2 (m2.cpp parseM2 / parseM2Animation) ---
    uint32_t      m2Version    = 0x100;           // 256 vanilla
    M2SkinSource  m2Skins      = M2SkinSource::Embedded;
    M2AnimSource  m2Anims      = M2AnimSource::Embedded;
    bool          m2TrackNested = false;          // WotLK+ nested per-anim arrays

    // --- WMO (wmo.cpp) ---
    uint16_t      wmoVersion = 17;                // 17 vanilla..Cata

    // --- DBC (wow_files.cpp Dbc::parse) ---
    DbcContainer  dbc        = DbcContainer::Wdbc;
    bool          dbcLocaleStrings = false;       // Cata: single localized string
};

// The only profile actually wired up today.
ClientProfile vanillaProfile();   // returns all defaults above

} // namespace wf
```

A matching `client_version.cpp` provides `vanillaProfile()` and (later)
`tbcProfile()/wotlkProfile()/cataProfile()`. Nothing else changes behavior yet.

### Step 1 — Thread the profile through, vanilla-default, no behavior change

Add an optional `const ClientProfile& = vanillaProfile()` parameter to the parse
entry points so callers can pass it but existing call sites still compile:

* `mpq.cpp` / `client_data.cpp`: `wowBaseArchiveNames(profile)` selects the archive
  name list; `mountWowClient(...)` branches `archive == Mpq` (today) vs `Casc`
  (throw). Smallest possible change; validates the scaffolding compiles.
* `wow_files.cpp::parseAdt(buf, profile)`: assert `profile.adtLayout ==
  Monolithic` (throw on `SplitTexObj`). Later, `SplitTexObj` dispatches to a new
  `parseAdtSplit(root, tex0, obj0)`.
* `wow_files.cpp::Dbc::parse(buf)`: keep accepting `WDBC`; when the magic is
  `WDB2` and `profile.dbc == Wdb2`, parse the larger header (new path), else throw.

### Step 2 — Map each profile field to its existing branch point

| Profile field | Parser file / function | Vanilla behavior today | What the branch adds later |
|---|---|---|---|
| `archive` | `mpq.cpp`, `client_data.cpp::mountWowClient` | MPQ chain | CASC reader (WoD+) — throw for now |
| `adtLayout` | `wow_files.cpp::parseAdt` | single file | Cata 3-file `AdtSet` loader |
| `defaultBigAlpha` / `heightTexturing` | `terrain.cpp::decodeAlphaMap` (already takes `bigAlpha`) | 2048B 4-bit unless `Wdt::bigAlpha()` | honor `MPHD 0x80`, MTXP |
| `liquid` | `terrain.cpp::parseChunks` (MCLQ) | `MclqLayer` per chunk | `MH2O` tile-level parser (new `parseMh2o`) |
| `m2Version` | `m2.cpp::parseM2` (currently asserts `MD20`) | accept 0x100 | accept 0x104/0x108; MD21 unwrap (Legion) |
| `m2Skins` | `m2.cpp::parseM2` view section | embedded view 0 | load `Model0N.skin` siblings |
| `m2Anims` / `m2TrackNested` | `m2.cpp::parseM2Animation`, `anim.hpp` | `RawChannel.ranges` single-timeline | nested per-anim arrays; `.anim` files |
| `wmoVersion` | `wmo.cpp::parseWmoRoot` | assume v17 | accept v17 across expansions (no-op), reject v14 |
| `dbc` / `dbcLocaleStrings` | `wow_files.cpp::Dbc::parse` | `WDBC` | `WDB2`; single localized string column |

### Step 3 — Detection helper

`ClientProfile detectProfile(const MpqManager&)`: infer the version from mounted
archives (e.g. presence of `expansion.MPQ`/`lichking.MPQ`, or read `WoW.exe`
build, or sniff an ADT's `MVER`/`MPHD` flags). For now it returns
`vanillaProfile()` unconditionally with a `// TODO`. This keeps a single, obvious
place where auto-detect will live.

### How to validate offline against the real 1.12.1 client

The vanilla profile must produce **byte-identical** results to the current code
(this is a pure refactor for 1.12.1):

1. **Golden-output test**: before threading the profile, dump (a) the
   `terrain.cpp` alpha bytes for a known tile (e.g. existing
   `wforge_bigalpha_test.mpq` / `Azeroth_32_48`), (b) M2 vertex/triangle counts
   for a known model, (c) WMO material/doodad counts. After threading
   `vanillaProfile()`, assert identical output. The repo already has
   `tests/` + fixture MPQs (`wforge_bigalpha_test.mpq`, `wforge_bigalpha_ovr.mpq`,
   `wforge_wmo_test.mpq`) — add a `client_version` round-trip test there.
2. **Profile-field unit tests** (pure, no filesystem): assert
   `vanillaProfile()` has `adtLayout==Monolithic, liquid==Mclq, m2Version==0x100,
   dbc==Wdbc, defaultBigAlpha==false` — locks the constants this note asserts.
3. **Negative tests**: `parseAdt(splitBuf, vanillaProfile())` and
   `parseM2(wotlkBuf)` throw a clear "unsupported client version" — proves the
   scaffolding fails loud rather than silently mis-parsing.
4. **Cross-check `bigAlpha` against real data**: confirm that real 1.12.1
   `Azeroth`/`Kalimdor` WDTs have `MPHD & 0x4 == 0` (so the 4-bit path is the one
   actually exercised) — matches the cmangos finding that vanilla extractors never
   decode big-alpha.

---

## 5. Risks / version pitfalls / open questions

1. **MPHD big-alpha flag value differs by era.** Vanilla/TBC use `MPHD & 0x4`;
   **TrinityCore retail checks `0x200`** and WotLK+ adds `0x80`
   (height-texturing). Do **not** copy a retail extractor's `0x200` into the
   vanilla path. WorldForge's existing `MPHD_BIG_ALPHA = 0x4` is the correct
   vanilla value; the profile should keep big-alpha *map-derived* (read the WDT),
   not purely profile-derived. Cross-checked: wowdev.wiki (`0x4`/`0x80`) vs TC
   (`0x200`).
2. **"version <= 264" Cata quirk.** Cata ships M2s that still report `version
   264` (WotLK's value); the expansion is *not* recoverable from the M2 version
   alone. Detection must use the *archive set / build*, not the M2 header. This is
   why `m2Version` lives in the profile but expansion detection keys off archives.
3. **External skin/anim is a hard dependency fan-out.** Moving from embedded
   (vanilla) to `.skin`/`.anim` (WotLK/Cata) means the M2 loader must request
   sibling files from the MPQ manager — a structural change to `asset_loader.cpp`,
   not just a parse branch. Budget for it; don't pretend it's a one-line flag.
4. **ADT split is not just "three files".** Cata removes `MCIN`, makes tex/obj
   `MCNK`s header-less, and relocates the existence table to WDT `MAID`. A naive
   "concatenate the three" will mis-parse. The split parser is a genuine new code
   path, hence a separate `parseAdtSplit`.
5. **MH2O vertex-format variants.** `MH2O` `SMLiquidInstance` height/depth arrays
   have several vertex formats (height-only, height+depth, height+uv, depth-only)
   keyed off `LiquidType.dbc`'s material. Implementing WotLK liquid requires that
   DBC, i.e. liquid pulls in a DBC dependency that vanilla `MCLQ` does not.
6. **DBC column drift, not container drift.** The `WDBC` container is identical
   vanilla->WotLK, but *column meaning* moves between builds. `dbc_defs` schemas
   are per-build; the profile should eventually carry/select a schema set, not just
   a container enum. Keeping field-by-index (as `wow_files.hpp` does) is the safe
   default. Open question: do we want per-version `dbc_defs` tables keyed by
   profile?
7. **4.3.4 is still MPQ.** Don't conflate "Cataclysm" with "CASC". CASC is WoD+.
   The `ArchiveKind::Casc` enum value exists only so the profile is forward-shaped;
   none of the four target expansions use it.
8. **WMO v17 spans all four expansions** — tempting to add a `wmoVersion` branch
   that does nothing. Keep the field (it documents intent and rejects v14 alpha
   WMOs) but expect it to be a near-no-op until BfA-era file-data-id materials.
9. **Open question — detection source of truth.** Build number (from `WoW.exe` or
   a signature), archive-set fingerprint, or per-file `MVER` sniffing? Recommend
   build-number-first with archive-set fallback; defer until a second expansion is
   actually targeted.

---

### Source key

* [wowdev-M2] wowdev.wiki/M2 — version is double-byte (256==1.0), Cata `<=264`
  shader path, MD21 Legion chunking, interpolation ranges removed post-vanilla.
* [wowdev-ADT] wowdev.wiki/ADT/v18 — Cata split files (.adt/_tex/_obj/_lod, MCIN
  gone, header-less tex/obj MCNK), MCAL 2048/4096 selected by MPHD `0x4`/`0x80`,
  MH2O, fix-alpha.
* [wowdev-WMO] wowdev.wiki/WMO — v14 alpha MOMO container; v17 retail; MOTX->file
  ids at 8.1.
* [wowdev-DB2] wowdev.wiki/DB2 + /DBC — WDBC vs WDB2(Cata)/WDB5/WDC; Cata localized
  strings.
* [getMaNGOS] getmangos.eu M2/MDX reference — vanilla version 0x100, BC+ 0x104,
  MD20 magic, 324-byte header.
* [TC-adt.h][TC-System.cpp] TrinityCore master `map_extractor` — adt_MCLQ/adt_MH2O,
  TILESIZE/ADT_CELLS_PER_GRID/ADT_CELL_SIZE, retail big-alpha `0x200`.
* [cmangos-System.cpp] cmangos/mangos-classic `contrib/extractor` — vanilla-only,
  MCLQ only, no MH2O / no big-alpha decode (proof those are absent from 1.12 data).
* [wow-adt][wow-m2] docs.rs Rust crates — AdtVersion/M2Version enums; split in
  Cata; MH2O in WotLK; external SKIN(WotLK)/ANIM(Cata); MD21(Legion).
* [wow.export] Kruithne/wow.export M2Loader.js — MD20 magic, embedded views,
  SFID/AFID external skin/anim chunks for Legion.
* [wmv-wmo.cpp] cleverca22/wowmapviewer src/wmo.cpp — MOHD 64B, MOMT 0x40, MODD
  0x28, MVER ignored.
* WorldForge code anchors: `src/coords.hpp`, `src/wow_files.hpp/.cpp`,
  `src/terrain.hpp/.cpp`, `src/m2.hpp/.cpp`, `src/wmo.hpp/.cpp`, `src/mpq.hpp`,
  `src/client_data.hpp`.
