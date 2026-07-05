#pragma once
// ---------------------------------------------------------------------------
// MCLQ liquid authoring: the editor's water/lava toolset over the parsed data
// model (terrain.hpp MapChunk / MclqLayer). Cell-mask set/clear, whole-tile
// fill, angled water planes, crop-below-terrain, automatic shore depth, and
// the exact 804-byte-per-layer MCLQ wire encoder for the ADT write path.
//
// Everything operates on mc.liquidLayers (ALL layers, kept in LQ-flag order:
// river, ocean, magma, slime) and keeps the MCNK header LQ flag bits plus the
// hasLiquid / liquidType / liquid back-compat mirror in sync after every
// mutation, so existing single-layer consumers (buildLiquidMesh, the render
// path) see edits immediately. Fully headless / unit-testable, in the same
// spirit as editing.hpp. Format facts verified against wowdev.wiki ADT/v18.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "math.hpp"
#include "terrain.hpp"

namespace wf {

// MCNK header LQ flag bit for a liquid type (MCNK_LQ_RIVER .. MCNK_LQ_SLIME);
// 0 for LiquidType::None.
uint32_t liquidTypeFlag(LiquidType type);

// MCLQ 8x8 tile-byte low-nibble code for a liquid type: 1 ocean, 3 slime,
// 4 river, 6 magma; 0xF ("don't render") for LiquidType::None. The high
// nibble carries the behaviour bits below and is preserved by the cell ops.
uint8_t liquidTypeNibble(LiquidType type);

// MCLQ tile-byte high-nibble behaviour bits.
constexpr uint8_t MCLQ_TILE_HIDDEN      = 0x0F;   // low nibble: cell not rendered
constexpr uint8_t MCLQ_TILE_FORCED_SWIM = 0x40;   // cell forces swimming
constexpr uint8_t MCLQ_TILE_FATIGUE     = 0x80;   // deep-water fatigue cell

// Auto-depth presets for autoDepth() below. The per-vertex depth byte is the
// shore-transparency ramp the vanilla client reads (0 = fully transparent
// shoreline, 255 = full water colour): depth = (liquidZ - terrainZ) * factor.
//   River: 12.75 => the byte saturates at 20 yd of water. Rivers are shallow,
//          so the shore fade resolves over a short range.
//   Ocean: 2.55  => saturates at 100 yd, a long open-water falloff.
constexpr float kRiverDepthFactor = 12.75f;
constexpr float kOceanDepthFactor = 2.55f;

// Find the chunk's layer for `type`, creating it if absent (find-or-create).
// A new layer starts entirely masked off (all 64 tile bytes 0x0F) with all
// heights/depths zero; its MCNK LQ flag bit is set, and when it is the
// chunk's first layer the hasLiquid/liquidType/liquid back-compat mirror is
// established. Layers are kept in LQ-flag order so the serializer and the
// parser agree on block order. `type` must not be None (None returns the
// back-compat mirror unmodified). The reference is invalidated by any later
// call that adds or drops a layer on the same chunk.
MclqLayer& ensureLiquidLayer(MapChunk& mc, LiquidType type);

// Enable liquid cells: for each set bit of `cellMask8x8` (bit = row*8 + col),
// write the type nibble into the tile byte (preserving the behaviour high
// nibble) and set the 9x9 vertex heights covering those cells to `height`
// (cell (r,c) touches vertices (r..r+1, c..c+1)). Creates the layer on
// demand, then refreshes min/max and the back-compat mirror.
void setLiquidCells(MapChunk& mc, LiquidType type, uint64_t cellMask8x8, float height);

// Disable liquid cells: each set bit's tile byte becomes 0x0F ("don't
// render"). When all 64 cells end up off, the layer is dropped entirely, its
// MCNK LQ flag cleared, and the next remaining layer (if any) promoted into
// the back-compat mirror. No-op if the chunk has no layer of that type.
void clearLiquidCells(MapChunk& mc, LiquidType type, uint64_t cellMask8x8);

// Whole-ADT fill: setLiquidCells over every chunk with a full mask at
// `height`. Returns the number of cells newly enabled across the tile (cells
// that were previously masked off or had no layer), so a repeat fill at the
// same height returns 0.
int fillTileWater(std::vector<MapChunk>& chunks, LiquidType type, float height);

// Whole-ADT wipe: drop every chunk's layer of `type`. Returns the number of
// chunks that carried one.
int clearTileLiquid(std::vector<MapChunk>& chunks, LiquidType type);

// Angled water (waterfall lips, sloped rapids): re-plane ALL 81 vertex
// heights of the chunk's `type` layer onto the tilted plane through `lock`:
//   height = lock.z + tan(angleDeg) * dot(vertexXY - lock.xy, dir(orientationDeg))
// Same plane convention as editing.hpp's FlattenPlane (orientation 0 = +X
// north, 90 = +Y west; angle clamped to [0,89]). Vertex world XY comes from
// the MCNK header position (the chunk's NW corner). No-op when the chunk has
// no layer of that type. Refreshes min/max and the mirror.
void tiltLiquid(MapChunk& mc, LiquidType type, Vec3 lock,
                float orientationDeg, float angleDeg);

// Disable every rendered cell whose four corner liquid heights are ALL
// strictly below the terrain surface at those corners -- the "crop water
// under the ground" cleanup after a terrain sculpt. Terrain is sampled at
// the chunk's 9x9 OUTER MCVT vertices (liquid vertex (r,c) pairs with outer
// height-grid vertex (r,c)); MCVT heights are relative to mc.position.z
// while liquid heights are absolute, so the base is added before comparing.
// A shoreline cell (liquid == terrain at some corner) is kept. Returns the
// number of cells removed; drops the layer (and LQ flag) if it empties.
int cropBelowTerrain(MapChunk& mc, LiquidType type);

// Recompute each liquid vertex's depth byte from the water column under it:
//   depth = clamp(round((liquidHeight - terrainHeightBelow) * factor), 0, 255)
// with terrain sampled at the paired 9x9 outer MCVT vertex (see
// cropBelowTerrain). This is the shoreline-transparency ramp the vanilla
// client reads per vertex; use kRiverDepthFactor / kOceanDepthFactor as
// sensible presets. Vertices at or below terrain clamp to 0 (transparent
// shoreline). Only meaningful for water layers: a documented no-op for
// magma/slime, whose vertex union carries texture coords instead of depth.
void autoDepth(MapChunk& mc, LiquidType type, float factor);

// Refresh a layer's minHeight/maxHeight envelope from the heights of the
// vertices belonging to its RENDERED cells (masked-off vertices often hold
// stale zeros and must not pollute the envelope -- buildLiquidMesh clamps to
// it). A fully masked layer gets min = max = 0.
void recomputeMinMax(MclqLayer& layer);

// Serialize ALL of the chunk's liquid layers to the exact MCLQ wire form:
// one 804-byte block per layer, in LQ-flag order (river, ocean, magma,
// slime). Per block: float min, max; 81 (9x9) vertices x 8 bytes -- water/
// ocean {u8 depth, u8 flow0Pct, u8 flow1Pct, u8 filler, float height} (flow
// percentages written 0; the editor does not author flows), magma/slime
// {u16 s, u16 t, float height}; then 64 (8x8) tile bytes; then u32 nFlowvs
// (written 0) and ALWAYS two 40-byte SWFlowv records on disk regardless of
// nFlowvs (written zeroed). Magma/slime texture coords are regenerated as
// s = col*32, t = row*32: with the 1.12 client's 3.0/256.0 ADT UV scale that
// spans uv 0..3 across the chunk, i.e. the lava texture tiles three times
// per chunk edge. Returns an empty vector for a chunk with no layers; two
// stacked layers (e.g. river + magma) emit 1608 bytes.
std::vector<uint8_t> encodeMclq(const MapChunk& mc);

// The MCNK_LQ_* bits that must be OR-ed into the MCNK header flags on save
// so a reader locates every block encodeMclq emitted. Derived from the
// layers actually present (not from mc.flags, which an edit session keeps in
// sync anyway) -- the two agree after any mutation through this module.
uint32_t mcnkLiquidFlags(const MapChunk& mc);

} // namespace wf
