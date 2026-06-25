#pragma once
// ---------------------------------------------------------------------------
// TexturePaintPanel: the editor's terrain texture-paint tool as a Dear ImGui
// panel -- the alpha-coverage counterpart to TerrainToolPanel. A thin wrapper
// over the GPU-independent paintAlpha brush (editing.hpp): paint or erase a
// blend layer's coverage under the cursor with a falloff profile.
//
// It edits a tile's per-chunk render AlphaMaps in place (TileRender::chunkAlphas)
// -- which the splat rasteriser samples directly -- so a stroke shows next frame
// with no re-mesh. A brush that straddles a chunk border paints into every chunk
// it touches (the centre mapped into each chunk's UV), so coverage stays seamless
// across the seam. As with the other panels, the brush-applying logic (paint())
// is separated from the ImGui draw calls so it is unit-testable headless.
//
// NOTE: this is the editor-side authoring path. "Layer" here is a per-chunk
// layer SLOT (1..3), not a texture -- the same slot may map to different textures
// in different chunks. Persisting paint back to MCAL on export is a separate,
// harder effort (MCAL is variable-length) and is deliberately not wired here.
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"
#include "terrain.hpp"   // MapChunk, AlphaMap
#include "editing.hpp"   // Falloff

namespace wf::editor {

class TexturePaintPanel {
public:
    // Render the panel ("Texture Paint" ImGui window).
    void draw();

    // Paint the current brush at world point `center` into the tile's per-chunk
    // render AlphaMaps. `chunkAlphas[c]` holds chunk c's blend maps for layer
    // slots 1.. (slot 0, the opaque base, carries none), parallel to `chunks`
    // (used for each chunk's grid position). blockX/blockY are the tile's WDT
    // indices. Returns the number of texels affected. No-op for slot < 1 or a
    // chunk that lacks the selected slot.
    int paint(std::vector<std::vector<AlphaMap>>& chunkAlphas,
              const std::vector<MapChunk>& chunks,
              int blockX, int blockY, Vec3 center) const;

    bool enabled() const { return enabled_; }

    // --- UI state (public for tests) ---
    bool  enabled_  = false;   // when on, the host paints on terrain drag
    int   layer_    = 1;       // 1-based layer slot to paint (1..3)
    float radius_   = 15.0f;   // brush radius in world yards
    float strength_ = 1.0f;    // 0..1 per stroke
    int   falloff_  = 2;       // index into the editing Falloff profiles (Smooth)
    bool  erase_    = false;   // paint toward 0 (erase) instead of 255 (paint)
};

} // namespace wf::editor
