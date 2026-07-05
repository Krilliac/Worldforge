#pragma once
// ---------------------------------------------------------------------------
// TerrainToolPanel: the editor's terrain sculpt tool as a Dear ImGui panel.
// A thin wrapper over the GPU-independent brush core (editing.hpp): raise,
// lower, flatten and smooth terrain-height brushes with a falloff profile.
// The host reads the UI state (enabled, mode, radius, strength, falloff) and,
// when sculpting is on, calls apply()/applyChunks() at the dragged world
// point each stroke STEP; a contiguous drag is one stroke.
//
// Undo wiring (nullable -- the panel works exactly as before without a
// stack): the FIRST applyChunks() of a stroke opens one AF_Heights action and
// every step copy-on-write pre-snapshots the chunks the brush reaches via
// touchChunk(); the host signals the end of the drag with endStroke(), which
// commits -- so an entire drag undoes in ONE step. A stroke that reached no
// chunk commits nothing.
//
// As with the other panels, the brush-applying logic is separated from the
// ImGui draw calls so it is unit-testable headless (apply()/applyChunks()),
// while draw() is the thin widget layer.
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"
#include "terrain.hpp"
#include "editing.hpp"        // Brush, FlattenPlane, FlattenMode
#include "command_stack.hpp"  // CommandStack (nullable)

namespace wf::editor {

class TerrainToolPanel {
public:
    enum class Mode { Raise, Lower, Flatten, Smooth };

    // Render the panel ("Terrain Tool" ImGui window).
    void draw();

    // Apply the current brush at world point `center` to a terrain mesh's
    // vertices (in place). For Flatten, level toward the height at `center`
    // (nearest vertex) -- or toward the angled lock plane when useLock_ is on.
    // Smooth has no mesh-vertex core (the chunk path is authoritative) and is
    // a no-op here. Returns the number of vertices affected.
    int apply(Mesh& mesh, Vec3 center) const;

    // Apply the current brush to a real tile's source MCNK height grids (MCVT)
    // in place -- the sculpt path for a loaded ADT tile, vs. apply() which edits
    // the procedural render mesh. blockX/blockY are the tile's WDT indices. For
    // Flatten, level toward `center.z` (the picked surface point) or the angled
    // lock plane. Returns the number of height samples affected. Re-mesh the
    // tile afterwards to see it.
    //
    // With a command stack set, the first call of a contiguous drag opens the
    // stroke's single undo action; `chunks` must stay alive (and un-moved)
    // until endStroke().
    int applyChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                    Vec3 center);

    // Host signals the end of a contiguous drag: commits the stroke's action
    // (one undo step for the whole drag). No-op without a stack or an open
    // stroke.
    void endStroke();

    // Nullable; without a stack the panel behaves exactly as before.
    void setCommandStack(CommandStack* stack) { stack_ = stack; }
    bool strokeOpen() const { return strokeOpen_; }

    bool enabled() const { return enabled_; }

    // --- UI state (public for tests) ---
    bool  enabled_  = false;   // when on, the host sculpts on terrain drag
    int   mode_     = 0;       // Mode: 0 Raise, 1 Lower, 2 Flatten, 3 Smooth
    float radius_   = 20.0f;
    float strength_ = 2.0f;    // yards applied PER STROKE STEP (each apply call)
    int   falloff_  = 0;       // index into the editing Falloff profiles

    // Flatten refinements (used by Mode::Flatten):
    int   flattenMode_    = 0;      // FlattenMode: 0 Both, 1 RaiseOnly, 2 LowerOnly
    float orientationDeg_ = 0.0f;   // angled plane: up-slope heading, 0..360
    float angleDeg_       = 0.0f;   // angled plane: tilt from horizontal, 0..89
    Vec3  lockPoint_;               // plane anchor when useLock_ is on
    bool  useLock_        = false;  // anchor the plane at lockPoint_ (else the pick)

private:
    // Build the brush from the current UI state, centred at `center`.
    Brush makeBrush(Vec3 center) const;
    // The flatten target plane: anchored at lockPoint_ when useLock_, else at
    // (center.xy, fallbackZ) -- with angle 0 that degenerates to the classic
    // "level toward the picked height".
    FlattenPlane makePlane(Vec3 center, float fallbackZ) const;

    // Stroke bookkeeping (all no-ops without a stack).
    void beginStrokeIfNeeded(std::vector<MapChunk>& chunks);
    void touchChunksInRange(const std::vector<MapChunk>& chunks, int blockX,
                            int blockY, Vec3 center);
    CaptureFns heightCapture() const;   // captures MCVT out of *strokeChunks_

    CommandStack*          stack_        = nullptr;
    std::vector<MapChunk>* strokeChunks_ = nullptr;   // live for the stroke only
    bool                   strokeOpen_   = false;
};

} // namespace wf::editor
