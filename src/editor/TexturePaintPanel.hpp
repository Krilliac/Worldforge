#pragma once
// ---------------------------------------------------------------------------
// TexturePaintPanel: the editor's terrain texture-paint tool as a Dear ImGui
// panel -- the alpha-coverage counterpart to TerrainToolPanel. A thin wrapper
// over the GPU-independent paintAlpha/sprayAlpha brushes (editing.hpp): paint
// or erase a blend layer's coverage under the cursor with a falloff profile,
// airbrush hardness/pressure, a target alpha level (Alt+1..5 hotkeys), and an
// optional spray scatter mode.
//
// It edits a tile's per-chunk render AlphaMaps in place (TileRender::chunkAlphas)
// -- which the splat rasteriser samples directly -- so a stroke shows next frame
// with no re-mesh. A brush that straddles a chunk border paints into every chunk
// it touches (the centre mapped into each chunk's UV), so coverage stays seamless
// across the seam. As with the other panels, the brush-applying logic (paint())
// is separated from the ImGui draw calls so it is unit-testable headless.
//
// Undo wiring (nullable -- the panel works exactly as before without a
// stack): the FIRST paint() of a stroke opens one AF_Alpha action and every
// step copy-on-write pre-snapshots the chunks the brush reaches; endStroke()
// commits, so an entire drag undoes in ONE step. Per the command stack's
// memory-bounding contract an alpha action never snapshots heights.
//
// NOTE: this is the editor-side authoring path. "Layer" here is a per-chunk
// layer SLOT (1..3), not a texture -- the same slot may map to different textures
// in different chunks. Persisting paint back to MCAL on export is a separate,
// harder effort (MCAL is variable-length) and is deliberately not wired here.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "math.hpp"
#include "terrain.hpp"        // MapChunk, AlphaMap
#include "editing.hpp"        // Falloff
#include "command_stack.hpp"  // CommandStack (nullable)

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
    //
    // With a command stack set, the first call of a contiguous drag opens the
    // stroke's single undo action; `chunkAlphas` must stay alive (and
    // un-moved) until endStroke().
    int paint(std::vector<std::vector<AlphaMap>>& chunkAlphas,
              const std::vector<MapChunk>& chunks,
              int blockX, int blockY, Vec3 center);

    // Host signals the end of a contiguous drag: commits the stroke's action.
    // No-op without a stack or an open stroke.
    void endStroke();

    // Nullable; without a stack the panel behaves exactly as before.
    void setCommandStack(CommandStack* stack) { stack_ = stack; }
    bool strokeOpen() const { return strokeOpen_; }

    // Alt+1..5 opacity hotkeys: slot 1..5 sets the target alpha to
    // 255/191/127/63/0. Pure state -- the host (or draw()) calls it on the
    // key press; out-of-range slots are ignored.
    void setOpacityHotkey(int slot);

    bool enabled() const { return enabled_; }

    // --- UI state (public for tests) ---
    bool  enabled_  = false;   // when on, the host paints on terrain drag
    int   layer_    = 1;       // 1-based layer slot to paint (1..3)
    float radius_   = 15.0f;   // brush radius in world yards
    float strength_ = 1.0f;    // legacy 0..1 rate; multiplies pressure_
    int   falloff_  = 2;       // index into the editing Falloff profiles (Smooth)
    bool  erase_    = false;   // paint toward 0 (erase) instead of the target

    // Airbrush refinements (wave-1 paintAlpha/sprayAlpha semantics):
    int   targetAlpha_ = 255;  // coverage the brush converges on (0..255)
    float hardness_    = 0.0f; // 0..1 fraction of the radius at full strength
    float pressure_    = 1.0f; // 0..1 application rate PER STROKE STEP
    bool  spray_       = false;          // scatter dabs instead of one stamp
    float sprayOuterRadius_ = 30.0f;     // yards: dab scatter disc
    float sprayDabRadius_   = 4.0f;      // yards: one dab's radius
    int   sprayDabsPerStep_ = 6;         // dabs placed per stroke step
    uint32_t sprayRng_ = 0x2F6E2B1u;     // session RNG state (advances per step)

private:
    // Stroke bookkeeping (all no-ops without a stack).
    void beginStrokeIfNeeded(std::vector<std::vector<AlphaMap>>& chunkAlphas);
    CaptureFns alphaCapture() const;   // captures ALL slots out of *strokeAlphas_

    CommandStack*                       stack_        = nullptr;
    std::vector<std::vector<AlphaMap>>* strokeAlphas_ = nullptr;   // stroke-scoped
    bool                                strokeOpen_   = false;
};

} // namespace wf::editor
