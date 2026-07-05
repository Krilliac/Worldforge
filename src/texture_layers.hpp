#pragma once
// ---------------------------------------------------------------------------
// MCLY texture-layer management: the editor's layer-table operations behind
// the texture-paint tool. A chunk blends up to four MCLY layers as a
// sequential lerp stack -- layer 0 is the opaque base coat (no alpha map),
// layers 1-3 each own a 64x64 coverage map in MCAL. This module manages that
// table (find-or-create a layer for a texture, remove one, retarget layers to
// another MTEX texture) plus the cross-layer normalisation that keeps the
// stack sensible after a paint stroke. Everything operates on the parsed data
// model (terrain.hpp MapChunk/TexLayer/AlphaMap and the Adt's MTEX string
// list), so it is fully unit-testable headless, like editing.hpp.
//
// Convention: `workingAlphas` is the decoded per-layer coverage owned by the
// paint session, paired 1:1 with mc.layers (workingAlphas[i] belongs to
// mc.layers[i]; index 0 is the base coat and its map is ignored -- the base
// is implicitly opaque). Edit the maps, then commitAlphas() to re-encode them
// into the chunk's MCAL blob.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "terrain.hpp"

namespace wf {

// Vanilla's hard cap: one base coat + three blended layers per chunk.
constexpr size_t MCLY_MAX_LAYERS = 4;

// MCLY effectId sentinel: no ground-effect doodads (else it names a
// GroundEffectTexture.dbc row).
constexpr uint32_t MCLY_NO_EFFECT = 0xFFFFFFFFu;

// Outcome of ensureLayer. On LayerCapReached the chunk already blends
// MCLY_MAX_LAYERS textures and nothing was mutated; the caller surfaces the
// error to the user ("cannot add a 5th layer") -- never silently truncates.
struct LayerEditResult {
    enum class Err { None, LayerCapReached };
    int  layerIndex = -1;      // index into mc.layers, or -1 on error
    bool created    = false;   // true if a new layer was appended
    Err  err        = Err::None;
};

// Find the chunk's layer using MTEX texture `mtexIndex`, or create it: a new
// TexLayer {textureId = mtexIndex, flags = MCLY_USE_ALPHA, effectId =
// MCLY_NO_EFFECT} plus a zeroed (fully transparent) working map. A layer
// created at index 0 becomes the base coat instead (flags 0), since the base
// carries no alpha map. When mc.layers is already at MCLY_MAX_LAYERS and the
// texture is not present, returns LayerCapReached and mutates nothing.
// Tolerates workingAlphas shorter than mc.layers (a chunk whose MCAL blob is
// empty decodes to zeros): it is padded with transparent maps to stay paired.
LayerEditResult ensureLayer(MapChunk& mc, std::vector<AlphaMap>& workingAlphas,
                            uint32_t mtexIndex);

// Find-or-append `path` in the ADT's MTEX filename list and return its index.
// The lookup is ASCII case-insensitive (MPQ paths are case-insensitive), so
// "Tileset\\X" and "TILESET\\x" name the same entry; a new path is appended
// verbatim. Separators are NOT normalised -- callers pass canonical
// backslash paths, as everywhere else in the loader.
int ensureMtexEntry(std::vector<std::string>& mtex, const std::string& path);

// Cross-layer normalisation, run (optionally -- it's an editor toggle) after
// raising layer `editedLayer`'s coverage: for each texel where the alpha-layer
// sum (layers 1..n-1; the base is excluded) exceeds 255, every layer OTHER
// than the edited one is scaled by (255 - edited) / (sum - edited),
// integer-rounded and clamped, so the stack sums back to <= 255 while the
// stroke the user just painted is preserved exactly.
void normalizeLayers(std::vector<AlphaMap>& workingAlphas, size_t editedLayer);

// Erase mc.layers[layerIndex] and its working map. Layer 0 (the base coat)
// is not removable; that and an out-of-range index return -1. Returns the
// new layer count on success. Re-encode with commitAlphas() afterwards.
int removeLayer(MapChunk& mc, std::vector<AlphaMap>& workingAlphas,
                size_t layerIndex);

// Scope of a texture swap: one chunk (chunkIndex) or the whole tile.
enum class SwapScope { Chunk, Tile };

// Re-theme in place: retarget every MCLY layer in scope that uses `fromPath`
// to `toPath` (find-or-appending toPath in MTEX). NO alpha data moves -- a
// fully painted zone swaps grass -> snow with every coverage map
// byte-identical, because only the textureId indices are rewritten. Swapping
// a chunk's layer-0 base texture is allowed (it just retargets). Returns the
// number of layers retargeted; 0 when fromPath is absent from MTEX, the two
// paths already name the same entry, or chunkIndex is out of range.
//
// Duplicate collapse: when the swap leaves a chunk with two layers naming the
// same texture (the target texture was already painted there), the duplicate
// is merged into the first occurrence by per-texel max of their coverage and
// erased -- the chunk's MCAL blob is then re-encoded (this is the ONE case
// where alpha bytes change; `bigAlpha` selects the encoding, vanilla false).
// Merging into the base coat degenerates to dropping the duplicate, since the
// base is implicitly opaque (max(255, x) == 255).
int swapTexture(std::vector<std::string>& mtex, std::vector<MapChunk>& chunks,
                const std::string& fromPath, const std::string& toPath,
                SwapScope scope, int chunkIndex = -1, bool bigAlpha = false);

// swapTexture over an explicit chunk-index list (brush scope). Out-of-range
// indices are skipped. Same retarget + duplicate-collapse semantics.
int swapTextureInChunks(std::vector<std::string>& mtex,
                        std::vector<MapChunk>& chunks,
                        const std::string& fromPath, const std::string& toPath,
                        const std::vector<int>& chunkIndices,
                        bool bigAlpha = false);

// Re-encode a paint session's working maps into the chunk: rebuilds the MCAL
// blob and refreshes each MCLY entry's use-alpha flag + ofsAlpha. Thin,
// name-giving wrapper over packAlphaLayers (terrain.hpp), which already keeps
// the offsets consistent. bigAlpha selects 8-bit (4096 B) vs vanilla packed
// 4-bit (2048 B, quantised to multiples of 17) encoding per layer.
void commitAlphas(MapChunk& mc, const std::vector<AlphaMap>& workingAlphas,
                  bool bigAlpha);

} // namespace wf
