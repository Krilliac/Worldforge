# M2 Animation: Texture/Color/Alpha Tracks, Particle & Ribbon Emitters (Vanilla 1.12.1)

Research note for WorldForge. Target: offline playback of M2 material animations
(color, alpha/transparency, UV transform) and simple particle/ribbon emitters in the
software rasteriser. Scope is **vanilla build 5875, M2 version 256 (0x100)** only.

---

## 1. Summary

WorldForge already parses static M2 geometry (`src/m2.cpp` → `M2Model`) and skeletal
bone animation (`src/m2.cpp::parseM2Animation` + `src/anim.hpp`). What is **not** yet
parsed or evaluated is the *material-level* animation that makes vanilla doodads and
spell effects look alive:

- **Color animations** (`M2Color`): an animated RGB tint + alpha, selected per render
  batch by a color index.
- **Texture-weight / transparency animations** (`M2TextureWeight`): an animated scalar
  opacity, selected per batch by a transparency index. This is how fade-in/out,
  flickering torches, glowing runes, etc. work.
- **Texture transforms** (`M2TextureTransform`, the "MTA"/`TexAnims` block): animated UV
  scroll/rotate/scale, selected per batch by a texture-transform index. This is how
  waterfalls, lava, scrolling clouds, portal swirls animate.
- **Particle emitters** (`M2ParticleOld`) and **ribbon emitters** (`M2Ribbon`): the
  CPU-simulated billboard sprites and trailing strips (fire, smoke, sparkles, sword
  glows, wisp trails).

All four are driven by the **same `M2Track`/AnimationBlock machinery** WorldForge already
reads for bones — keyframed (timestamp, value) arrays with an interpolation type and an
optional **global sequence** index. The vanilla on-disk layout uses *single* timestamp/
value arrays plus a per-animation `ranges[]` table — exactly the `RawChannel<T>` shape
already in `src/m2.hpp`. So the parser work is mostly "more of the same channels," and
the renderer work is: (a) modulate the existing textured triangle path by a per-batch
animated color+alpha and animated UVs; (b) add a tiny CPU particle simulator that emits
camera-facing quads into the existing `TexMesh` + `rasterTexMesh` pipeline with additive/
alpha blending.

Why it matters: this is the difference between "static props placed in the world" and
"the world that breathes" — and it is the last big M2 feature gap before WorldForge can
render a recognisable vanilla scene (campfires, braziers, water, spell visuals) offline.

---

## 2. Concrete format facts (vanilla 1.12.1, version 256)

### 2.1 Header offsets (the new arrays we need)

The vanilla `ModelHeader` is a flat list of `(count:uint32, offset:uint32)` pairs.
`nViews` is a **single uint32** (skin profiles are embedded in vanilla — already handled
in `m2.cpp`), which shifts everything after it by 4 bytes relative to a naive read. The
offsets below are confirmed against **getMaNGOS' M2/MDX wiki** and **WoWModelViewer's
`src/games/wow/modelheaders.h`** (`struct ModelHeader`), which agree field-for-field.

| Field (count at hex; offset = count+4) | Hex (count) | Meaning |
|---|---|---|
| `nGlobalSequences / ofs` | 0x10 | global-sequence durations (uint32 ms each) |
| `nAnimations / ofs` | 0x18 | sequences (already parsed) |
| `nAnimationLookup / ofs` | 0x20 | anim-id → sequence index |
| `nBones / ofs` | 0x28 | bones (already parsed) |
| `nKeyBoneLookup / ofs` | 0x30 | key-bone lookup |
| `nVertices / ofs` | 0x38 | vertices (already parsed) |
| `nViews` (**single uint32**) | 0x40 | embedded skin profile count |
| **`nColors / ofsColors`** | **0x44 / 0x48** | `ModelColorDef[]` (RGB + alpha tracks) |
| `nTextures / ofs` | 0x4C | textures (already parsed) |
| **`nTransparency / ofsTransparency`** | **0x54 / 0x58** | `ModelTransDef[]` (texture-weight tracks) |
| **`nTexAnims / ofsTexAnims`** | **0x5C / 0x60** | `ModelTexAnimDef[]` (UV transforms) |
| `nTexReplace / ofs` | 0x64 | replaceable-texture lookup |
| `nRenderFlags / ofs` (a.k.a. materials) | 0x6C | `(flags:uint16, blendingMode:uint16)[]` |
| `nBoneLookup / ofs` | 0x74 | bone lookup table |
| `nTexLookup / ofs` | 0x7C | texture lookup table |
| `nTexUnitLookup / ofs` | 0x84 | texture-unit lookup |
| **`nTransparencyLookup / ofs`** | **0x8C / 0x90** | transparency lookup table |
| **`nTexAnimLookup / ofs`** | **0x94 / 0x98** | texture-anim lookup table |
| (collision sphere + bound sphere + bounding tris/verts/normals) | 0x9C… | skip |
| `nAttachments / ofs`, `nAttachLookup / ofs` | | attachments |
| `nEvents / ofs`, `nLights / ofs`, `nCameras / ofs`, `nCameraLookup / ofs` | | |
| **`nRibbonEmitters / ofsRibbonEmitters`** | (~0x134 / 0x138) | `M2Ribbon[]` |
| **`nParticleEmitters / ofsParticleEmitters`** | (~0x13C / 0x140) | `M2ParticleOld[]` |

> NOTE: The exact hex of the ribbon/particle pairs (0x134/0x13C region) depends on whether
> the bounding-sphere block is `Sphere{Vec3 min,max; float r}` (WMV) vs `Vec3+float`
> (wowdev). **Do not hardcode 0x134/0x13C** — walk the header field-by-field from 0x9C
> reading every pair in order (the order is fixed: bounds → attachments → attachLookup →
> events → lights → cameras → cameraLookup → ribbons → particles → unknown). This is
> the robust approach and matches how WMV/pywowlib read it.
> *Sources: getMaNGOS "M2 MDX Files" wiki; WoWModelViewer `modelheaders.h` `struct ModelHeader`.*

### 2.2 AnimationBlock / M2Track (vanilla layout — already in code)

Vanilla `AnimationBlock` (WMV `struct AnimationBlock`, 28 bytes — matches WorldForge's
`kAnimBlock = 0x1C`):

```
int16  interpolation_type   // 0=none/step, 1=linear, 2=hermite/bezier (rare)
int16  global_sequence      // -1 if none, else index into global_sequences
M2Array ranges    (uint32 n, uint32 ofs)  // per-animation [first,last] index pairs (M2Range = 2×uint32)
M2Array timestamps(uint32 n, uint32 ofs)  // uint32 ms
M2Array values    (uint32 n, uint32 ofs)  // T
```

This is **exactly** `RawChannel<T>` + `readChannel<T>()` in `src/m2.cpp`. The only thing
new vs bones is the **value type T** per track and the **global-sequence** path (see 2.6).
*Sources: WMV `animated.h` (`Animated<T>::getValue`), wow.export `M2Loader.js readM2Track`,
pywowlib `M2TrackBase`, three-m2loader `M2Track`.*

### 2.3 Colors — `ModelColorDef` (`nColors`)

```
struct ModelColorDef {       // 56 bytes (2 × 28-byte AnimationBlock)
    AnimationBlock color;    // value type = Vec3  (RGB, linear 0..1, 12 bytes/value)
    AnimationBlock opacity;  // value type = fixed16 (int16, alpha = value / 32767, 2 bytes/value)
};
```
Selection: a render batch carries a **color index**; if valid, the batch's final colour is
`color.sample(t) (RGB) × opacity.sample(t)/32767 (A)`, multiplied into the texture/material.
*Sources: WMV `ModelColorDef`; wow.export `parseChunk_MD21_colors` (color=3 floats,
alpha=int16); pywowlib `M2Color{color:M2Track(vec3), alpha:M2Track(fixed16)}`; three-m2loader.*

### 2.4 Transparency / texture-weight — `ModelTransDef` (`nTransparency`)

```
struct ModelTransDef {       // 28 bytes
    AnimationBlock trans;    // value type = fixed16 (int16); weight = value / 32767
};
```
Selection: a render batch carries a **transparency index**; the batch's opacity is
multiplied by `trans.sample(t)/32767`. (This is the per-material "texture weight" / overall
alpha — distinct from per-pixel texture alpha.) Indirection: `header.transparency_lookup[batch.transparencyIndex]`
in some readers; in vanilla the batch field usually already points straight at the trans
array — verify against a real file (see §4).
*Sources: WMV `ModelTransDef` + `ShortToFloat = t/32767.0f` in `animated.h`; wow.export
`parseChunk_MD21_textureWeights` (int16); pywowlib `M2TextureWeight` (M2Track fixed16);
three-m2loader (`value/0x7fff` → `material.opacity`).*

### 2.5 Texture transforms (MTA) — `ModelTexAnimDef` (`nTexAnims`)

```
struct ModelTexAnimDef {     // 84 bytes (3 × 28-byte AnimationBlock)
    AnimationBlock translation;  // Vec3  (use .x,.y for UV; 12 bytes/value)
    AnimationBlock rotation;     // Quat (Vec4, 16 bytes/value) — UV rotation about Z
    AnimationBlock scaling;      // Vec3  (use .x,.y; 12 bytes/value)
};
```
Application (per batch with a valid **texture-transform index**): build a 2D UV matrix
`UV' = T(trans.xy) * R(rotation about Z, centred at (0.5,0.5)) * S(scale.xy) * UV`.
three-m2loader maps this to `map.offset` (translation.xy) + `map.rotation` (quaternion→angle)
+ scale; WMV builds a full 4×4 texture matrix.
*Sources: WMV `ModelTexAnimDef`; wow.export `parseChunk_MD21_textureTransforms`
(translation=3f, rotation=4f, scaling=3f); pywowlib `M2TextureTransform`; three-m2loader.*

### 2.6 Global sequences

`global_sequences` is an array of `uint32` durations (ms). When a track's
`global_sequence >= 0`, the track ignores the current animation and instead samples at
`globalTimeMs % global_sequences[seq]` (using value-array index 0 / the whole track rather
than a per-animation range). This is how torches flicker independently of the model's
"Stand" animation. WMV: `if (seq>=0){ time = globals[seq]? globalTime%globals[seq] : 0; anim=0; }`.
*Sources: WMV `animated.h` `Animated<T>::getValue`; wowdev M2 "Global sequences".*

### 2.7 Render-batch material fields (skin profile, embedded in vanilla view 0)

The per-batch (texture-unit) record inside the embedded skin profile carries the indices
that select the above animations. Vanilla `M2Batch` fields (relevant subset):

```
uint16 flags;
int16  shaderId / unused;
uint16 submeshIndex;        // which M2SkinSection this batch draws
uint16 submeshIndex2;
int16  colorIndex;          // -1 or → ModelColorDef[]      (color+alpha animation)
uint16 materialIndex;       // → renderFlags[]  (flags + blendingMode)  ← BLEND MODE lives here
uint16 materialLayer / priorityPlane;
uint16 textureComboIndex;       // → texture_lookup_table[]
uint16 textureCoordComboIndex;  // → tex_unit_lookup
uint16 textureWeightComboIndex; // → transparency_lookup_table[] (texture-weight/transparency)
uint16 textureTransformComboIndex; // → texture_transform_lookup_table[]
```
`renderFlags[materialIndex] = (uint16 flags, uint16 blendingMode)` where **blendingMode**
is the M2 blend enum (see §2.9). `flags` bit values: 0x01 unlit, 0x04 two-sided,
0x10 depth-test off, 0x20 depth-write off.
*Sources: wowdev M2/.skin `M2Batch`; WMV `model.cpp` pass setup; cmangos/Noggit batch reads.
EXACT field offsets/strides must be confirmed against a real 1.12 .m2 — see §4.*

### 2.8 `M2ParticleOld` — vanilla particle emitter

Vanilla/pre-WotLK particle struct (version ≤ 263). It differs from WotLK: blending and
emitter type are **uint16** (WotLK packs them into uint8s), and the per-particle color/
scale/lifespan are stored as **inline `mid_point` + value arrays** rather than the WotLK
`M2PartTrack`/`FBlock`. Read order (pywowlib `M2Particle` ≤TBC; WMV `M2ParticleDef`):

```
uint32 particle_id;
uint32 flags;
Vec3   position;             // emitter position in bone space
uint16 bone;                 // parent bone (billboard origin follows this bone's pose)
uint16 texture;              // index into textures[]
M2Array geometry_model_filename;   // (char) usually empty in vanilla
M2Array recursion_model_filename;  // (char) usually empty
uint16 blending_type;        // (vanilla uint16) → blend enum, see §2.9
uint16 emitter_type;         // 1=Plane, 2=Sphere, (3=Spline rare)
uint16 particle_color_index;
... texture_tile_rotation (uint16), rows (uint16), cols (uint16) ...
AnimationBlock emission_speed;      // float
AnimationBlock speed_variation;     // float (0..1 fraction)
AnimationBlock vertical_range;      // float radians (cone)
AnimationBlock horizontal_range;    // float radians
AnimationBlock gravity;             // float
AnimationBlock lifespan;            // float seconds
AnimationBlock emission_rate;       // float particles/sec
AnimationBlock emission_area_length;// float (plane emitter)
AnimationBlock emission_area_width; // float
AnimationBlock z_source;            // float
float  mid_point;                   // PRE-WOTLK ONLY: split of life ramp (colors[0..1] vs [1..2])
Color/uint colors[3]    (BGRA or float RGBA × 3);  // life-stage colours
float scales[3];                    // life-stage sizes
uint16 lifespanUVSlots? / headCellBegin/End ...    // tiling/anim cells
... tail/flags, twinkle, spin (single float in vanilla) ...
AnimationBlock enabled_in;          // visibility (per animation) — IMPORTANT for offline gating
```
Key behavioural fields for a minimal sim: `position`, `bone`, `texture`, `blending_type`,
`emitter_type`, `rows`/`cols`, `emission_speed`, `speed_variation`, `lifespan`,
`emission_rate`, `gravity`, `vertical/horizontal_range`, `emission_area_length/width`,
`mid_point`, `colors[3]`, `scales[3]`.

> The **exact byte size** of `M2ParticleOld` in vanilla is the single most version-sensitive
> number here and is *not* safe to copy from a WotLK struct. Determine the stride empirically:
> `(next array offset − ofsParticleEmitters) / nParticleEmitters`, or step through one known
> vanilla file. WMV's `M2ParticleDef` as exposed today is the *merged/modern* layout (it
> contains `multiTextureParam` WotLK fields) and must NOT be used as the vanilla stride.
*Sources: pywowlib `M2Particle` (≤TBC gating: uint16 blend/emitter, mid_point + color/scale
arrays, single spin); wowdev M2 `M2ParticleOld`; WMV `particle.cpp` `ParticleSystem::init`
(reads emission tracks, rows/cols, colors[3]/sizes[3]).*

### 2.9 Blend / `blendingMode` enum (shared by materials and particles)

```
0 Opaque            // no blend, alpha-test off
1 AlphaKey/Mod      // alpha test (cutout); src=ONE? — used as "Mod" / 1-bit alpha
2 Alpha             // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA  (standard translucency)
3 Add               // GL_SRC_COLOR (or SRC_ALPHA), GL_ONE     (additive glow)
4 AddAlpha          // GL_SRC_ALPHA, GL_ONE
5 Modulate          // GL_DST_COLOR, GL_ZERO
6 Modulate2x        // GL_DST_COLOR, GL_SRC_COLOR
```
For particles the value comes from `M2ParticleOld.blending_type`; for mesh batches from
`renderFlags[materialIndex].blendingMode`. WorldForge's `rasterTexMesh(... alphaBlend)`
already does mode-2 (over) and opaque/alpha-test; **additive (mode 3/4) is the main new
mode needed for fire/glow**. *Source: WMV `particle.cpp` `ParticleSystem::draw` switch;
wowdev M2/Rendering.*

### 2.10 `M2Ribbon` — vanilla ribbon emitter

```
int32  ribbon_id;
uint32 bone_index;
Vec3   position;
M2Array texture_indices (uint16);   // textures used
M2Array material_indices (uint16);  // renderFlags used
AnimationBlock color;     // Vec3 RGB
AnimationBlock alpha;     // fixed16
AnimationBlock height_above; // float
AnimationBlock height_below; // float
float  edges_per_second;  // emission rate of strip segments
float  edge_lifetime;     // seconds a segment persists
float  gravity;
uint16 texture_rows, texture_cols;
AnimationBlock tex_slot;   // uint16 (which tile)
AnimationBlock visibility; // uint8/fixed (per-animation on/off)
// vanilla: NO priorityPlane / padding tail that WotLK adds
```
Render: a deque of segments; each frame, if the bone's world position moved more than the
segment length, push a new segment (pos, up vector from `height_above`/`height_below`,
cumulative length). Draw as a quad strip with V running along accumulated length / total
length. *Sources: WMV `ModelRibbonEmitterDef` + `RibbonEmitter::setup/draw`; pywowlib
`M2Ribbon` (≤TBC, no priority_plane).*

---

## 3. How other engines implement it

### WoWModelViewer (C++, the canonical vanilla-capable renderer)
- `src/games/wow/modelheaders.h` — `struct ModelHeader`, `AnimationBlock`, `ModelColorDef`,
  `ModelTransDef`, `ModelTexAnimDef`, `ModelRibbonEmitterDef`, `M2ParticleDef`.
- `src/games/wow/animated.h` — `template Animated<T>` with `getValue(anim, time)`:
  global-sequence redirect (`time = globalTime % globals[seq]`), per-anim `data[anim]`/
  `times[anim]` arrays, `INTERPOLATION_NONE`/`LINEAR`, helpers `ShortToFloat (t/32767)`,
  `Quat16ToQuat32`. Internally stores vector-of-vectors *after* converting the on-disk
  single-array+ranges layout.
- `src/games/wow/model.cpp` — `ModelColor`/`ModelTransparency`/`ModelTexAnim` runtime
  objects; per render pass picks `colorIndex`/`opacity`/`texanim` and feeds GL.
- `src/games/wow/particle.cpp` — `ParticleSystem::init/update/draw`,
  `PlaneParticleEmitter`/`SphereParticleEmitter::newParticle`, `RibbonEmitter`.
  - **Billboarding:** `vRight=(mv[0],mv[4],mv[8])`, `vUp=(mv[1],mv[5],mv[9])`; quad =
    `pos ± (vRight±vUp)*size`.
  - **Life ramp:** `rlife = life/maxlife`; `lifeRamp(rlife, mid, a,b,c)` blends colors[3]/
    sizes[3] across the particle's life with the `mid_point` split.
  - **Blend switch:** maps blend enum → `glBlendFunc` (Add: `SRC_COLOR,ONE`; AddAlpha:
    `SRC_ALPHA,ONE`; Alpha: `SRC_ALPHA,ONE_MINUS_SRC_ALPHA`).
  - **Tiling:** `initTile` builds UV sub-rects from `rows`/`cols`; each particle picks a
    random `tile`.
  - **Ribbon:** `GL_QUAD_STRIP`, V = cumulative seg length / total length.

### wow.export (JS) — `src/js/3D/loaders/M2Loader.js`
- `readM2Track()` (interp uint16, globalSeq uint16, timestamps M2Array, values M2Array);
  `parseChunk_MD21_colors` (color=3f, alpha=int16), `parseChunk_MD21_textureWeights`
  (int16), `parseChunk_MD21_textureTransforms` (translation 3f / rotation 4f / scaling 3f).

### pywowlib (Python) — `file_formats/m2_format.py`
- Authoritative for **version gating**: `M2TrackBase` includes `interpolation_ranges` for
  ≤TBC (the single-array+ranges layout WorldForge uses); `M2Particle` ≤TBC uses uint16
  blend/emitter, `mid_point` + `color_values`/`scale_values` arrays + single `spin`;
  `M2Ribbon` ≤TBC lacks `priority_plane`.

### three-m2loader (JS) — modern, shows material mapping
- fixed16 → `value/0x7fff`; texture-weight → `material.opacity`; color → `material.color`;
  texture-transform → `map.offset`(translation.xy) + `map.rotation`(quat→angle) + scale.

---

## 4. Implementation plan for WorldForge (smallest-first)

Existing scaffolding makes this incremental: `RawChannel<T>` + `readChannel<T>()` already
read AnimationBlocks, `KeyTrack<T>::sample` already does step/linear sampling, and
`rasterTexMesh(..., alphaBlend)` already alpha-composites. We extend, not rewrite.

**Step 0 — Validation harness (do first).** Pick 2–3 known vanilla files from the real
client: a flickering torch/brazier doodad (color+global-seq alpha), an animated-UV surface
(`World/...` water/lava doodad), and a fire/sparkle particle doodad
(`Spells/` or `Creature/.../*.m2`). Print, via a throwaway `m2_demo` path: header field walk
with computed offsets, each new array's count, and the first track's (interp, globalSeq,
nRanges, nTimes, nValues). This both confirms the §2 offsets/strides *for the real 5875
build* and gives golden values to diff against.

**Step 1 — Parse global sequences + color/transparency/texanim tracks (`src/m2.cpp`,
`src/m2.hpp`).**
- Add to `M2Animation` (or a new `M2Materials` struct): `std::vector<uint32> globalSeqs;`
  `std::vector<M2Color>`, `std::vector<M2TextureWeight>`, `std::vector<M2TextureTransform>`,
  plus the lookup tables (`transparencyLookup`, `texAnimLookup`) and `renderFlags`
  (`vector<pair<uint16,uint16>>`).
- Reuse `readChannel<T>()`. New value decoders: `fixed16` (read int16 → `v/32767.0f`),
  `Vec3` (already have), `Quat` (already have, for UV rotation use only Z component).
- `M2Color = { RawChannel<Vec3> rgb; RawChannel<float> alpha; }` (alpha via fixed16 reader).
- `M2TextureWeight = { RawChannel<float> weight; }`.
- `M2TextureTransform = { RawChannel<Vec3> translation; RawChannel<Quat> rotation;
  RawChannel<Vec3> scaling; }`.
- **Walk the header from 0x9C** to robustly locate ribbon/particle offsets (don't hardcode).
- Validate: counts + first-track stats match Step 0 golden output.

**Step 2 — Evaluate tracks with global-sequence support (`src/anim.hpp` / new
`src/m2_material.hpp`).**
- Generalise `KeyTrack<T>::sample` usage: add a `sampleTrack(channel, animIndex, animTimeMs,
  globalTimeMs, globalSeqs, fallback)` that, when `channel.globalSeq >= 0`, samples at
  `globalTimeMs % globalSeqs[globalSeq]` over the whole track; else slices to
  `ranges[animIndex]` (the logic already in `buildBonesForAnimation`, factored out).
- Add `fixed16` interpolation (plain float lerp) — `interpolate(float,float,f)` is trivial.
- Unit-check: torch alpha track sampled at t=0..duration produces the expected 0→1→0 ramp.

**Step 3 — Per-batch animated color + alpha in the mesh path (`src/m2.hpp`, `src/m2_render.*`).**
- Parse the embedded skin-profile **batches** (currently `m2.cpp` reads submeshes but not
  the texture-unit/batch records). Add `M2Batch{submeshIndex, colorIndex, materialIndex,
  textureWeightIndex, textureTransformIndex, textureComboIndex}`.
- In `skinM2`/`poseM2`, group triangles by batch. For each batch compute a constant-per-frame
  `Rgba tint` = `color.rgb(t) × (color.alpha(t) × textureWeight(t))` and a blend mode from
  `renderFlags[materialIndex].blendingMode`.
- Extend `TexVertex`/`TexMesh` (or pass a per-draw `Rgba modulate` + blend flag to
  `rasterTexMesh`) so the rasteriser multiplies texel × tint. Map blend mode 2 → existing
  `alphaBlend`, mode 3/4 → **new additive path** (texel scaled by alpha/colour, added to
  framebuffer, no depth write). This is the one genuinely new rasteriser feature.
- Validate offline: render the torch doodad across its global-seq period → screenshot shows
  the flame fading/pulsing; compare silhouette/alpha against WMV on the same model.

**Step 4 — Texture-transform (animated UVs) in the mesh path (`src/m2_render.cpp`).**
- For batches with a valid texture-transform index, build a 2×3 (or 3×3) UV matrix from
  `translation.xy / rotation.z / scaling.xy` (rotate about (0.5,0.5)) and apply it to each
  vertex UV when building the `TexMesh` for that batch (cheap: bake per-frame UVs).
- Validate: a scrolling water/lava doodad shows UVs marching; offset magnitude matches the
  translation track derivative.

**Step 5 — Minimal particle simulator (new `src/m2_particles.{hpp,cpp}`).**
- Parse `M2ParticleOld[]` (use the empirically-confirmed vanilla stride from Step 0).
- A `ParticleSystem` per emitter holds a `std::vector<Particle>` (pos, velocity, age,
  maxLife, tile). Per fixed timestep `dt`:
  - emit `emission_rate(t) * dt` particles at `position` (in the parent bone's world frame
    from `computePose`), with initial speed `emission_speed(t)*(1±speed_variation)` directed
    by a cone from `vertical_range`/`horizontal_range` (Plane) or random sphere dir (Sphere);
  - integrate `vel.z -= gravity*dt; pos += vel*dt; age += dt`; cull `age >= maxLife`.
  - per particle: `rlife = age/maxLife`; `size = lifeRamp(rlife, mid_point, scales[0..2])`;
    `color = lifeRamp(rlife, mid_point, colors[0..2])`.
- Build camera-facing quads: `right/up` from the **view matrix** (WorldForge already has the
  camera/MVP in the demo). Emit two triangles per particle into a `TexMesh`, UVs from the
  `rows/cols` tile. Render with the particle's `blending_type` (additive for fire/sparkle)
  via the Step-3 additive rasteriser path. No depth write; sort back-to-front optional for
  mode-2, unnecessary for additive.
- Validate: a campfire/sparkle doodad emits a believable plume offline; particle count and
  lifetime sane vs WMV (exact match impossible — it's stochastic — but envelope should match).

**Step 6 (optional) — Ribbons (`src/m2_particles.cpp`).** Deque of segments keyed on the
parent bone's world position each frame; emit a quad strip with V = cumulative/total length,
width from `height_above+height_below`, modulated by the ribbon color/alpha tracks. Lower
priority than particles for a recognisable scene.

**Determinism note:** seed the particle RNG per emitter so offline frames are reproducible
(important for screenshot diffing in CI).

---

## 5. Risks / version pitfalls / open questions

1. **`M2ParticleOld` stride is the big unknown.** WMV's currently-published `M2ParticleDef`
   is the *merged modern* layout (has WotLK `multiTextureParam`/`fp_2_5` fields) and will
   give the wrong vanilla size. Trust **pywowlib's ≤TBC gating** (uint16 blend/emitter,
   `mid_point` + colors[3]/scales[3] inline, single `spin`, no priorityPlane) and **measure
   the stride from a real 5875 file** (Step 0). Cross-check on ≥2 files of differing
   `nParticleEmitters` to disambiguate.
2. **Header tail offsets (ribbons/particles) are not safely constant.** The bounding-sphere/
   collision block size differs between sources; *walk the header pairs in order* rather
   than hardcoding 0x134/0x13C.
3. **`nViews` single-uint32 quirk** already bit the existing parser correctly (it's at 0x40);
   every offset in §2.1 assumes that. If a future refactor reads `nViews` as a pair,
   everything after shifts by 4 — keep the single-uint32 read.
4. **Blend-mode semantics for "Add".** Sources disagree whether additive uses `SRC_COLOR`
   or `SRC_ALPHA` as the source factor (WMV `BM_ADDITIVE` uses `GL_SRC_COLOR,GL_ONE`;
   `BM_ADDITIVE_ALPHA` uses `GL_SRC_ALPHA,GL_ONE`). For a software rasteriser, implement both
   as `dst += texel.rgb * factor`; pick factor = alpha for mode 4, colour for mode 3.
5. **Lighting/fog flags.** Particle `flags` and material `flags` carry "unlit"/"fog" bits;
   for offline, treat lit particles as emissive (skip the directional-light modulation the
   mesh path applies) so fire doesn't get shaded dark.
6. **Texture-weight vs texture-alpha conflation.** `M2TextureWeight` is *per-material* scalar
   opacity (animated), separate from the BLP's per-texel alpha and from `M2Color.alpha`.
   Final pixel alpha = `texelAlpha × colorAlpha × textureWeight`. Don't drop any factor.
7. **Indirection through lookup tables.** Whether the batch field indexes the
   color/trans/texanim array *directly* or via `*_lookup_table` is a known footgun and
   differs subtly in vanilla; confirm with Step 0 by checking that
   `batch.textureWeightIndex` (raw) vs `transparency_lookup[batch.textureWeightIndex]`
   yields an in-range `nTransparency` index on a real file.
8. **Spline emitter type (3)** and recursion-model particles are rare in vanilla and can be
   skipped initially (render nothing) without visible loss.
9. **Hermite/Bezier interpolation (type ≥ 2)** appears in a few tracks; linear is a safe
   fallback for v1 and visually close for short M2 tracks.

---

### Primary sources (cross-checked, ≥2 independent per fact)
- wowdev.wiki **M2** and **M2/.skin**, **M2/Rendering** (format reference; blocked to
  WebFetch but corroborated below).
- **getMaNGOS** "M2/MDX Files" client-file wiki — vanilla header offsets (0x44 colors,
  0x54 transparency, 0x5C texAnims, 0x8C/0x94 lookups, ribbon/particle pairs).
- **WoWModelViewer** `modelheaders.h` (`ModelHeader`, `AnimationBlock`, `ModelColorDef`,
  `ModelTransDef`, `ModelTexAnimDef`, `ModelRibbonEmitterDef`), `animated.h`
  (`Animated<T>::getValue`, `ShortToFloat=t/32767`), `particle.cpp`
  (`ParticleSystem` billboarding/lifeRamp/blend/tiling, `RibbonEmitter`).
- **pywowlib** `file_formats/m2_format.py` — version gating (≤TBC interpolation_ranges;
  `M2Particle`/`M2Ribbon` old layouts).
- **wow.export** `src/js/3D/loaders/M2Loader.js` — `readM2Track`, colors/textureWeights/
  textureTransforms value types.
- **three-m2loader** `M2Loader.js` — fixed16 `value/0x7fff`, material mapping of
  weight/color/transform.

### Maps to existing WorldForge files
- `src/m2.cpp` / `src/m2.hpp` — extend header walk + new track arrays (Steps 1, 3).
- `src/anim.hpp` — factor out range/global-seq sampling, add fixed16 lerp (Step 2).
- `src/m2_render.cpp` / `src/m2_render.hpp` — per-batch tint/blend + animated UVs (Steps 3–4).
- `src/raster.hpp` / raster impl — new additive blend path (Step 3).
- **new** `src/m2_material.hpp` (material-anim eval), `src/m2_particles.{hpp,cpp}`
  (Steps 5–6), exercised via `src/m2_demo.cpp`.
