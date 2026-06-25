#pragma once
// ---------------------------------------------------------------------------
// TerrainToolPanel: the editor's terrain sculpt tool as a Dear ImGui panel.
// A thin wrapper over the GPU-independent brush core (editing.hpp): raise,
// lower and flatten terrain-height brushes with a falloff profile. The host
// reads the UI state (enabled, mode, radius, strength, falloff) and, when
// sculpting is on, calls apply() at the dragged world point each stroke.
//
// As with the other panels, the brush-applying logic is separated from the
// ImGui draw calls so it is unit-testable headless (apply()), while draw() is
// the thin widget layer.
// ---------------------------------------------------------------------------
#include "math.hpp"
#include "terrain.hpp"
#include "editing.hpp"   // Brush

namespace wf::editor {

class TerrainToolPanel {
public:
    enum class Mode { Raise, Lower, Flatten };

    // Render the panel ("Terrain Tool" ImGui window).
    void draw();

    // Apply the current brush at world point `center` to a terrain mesh's
    // vertices (in place). For Flatten, level toward the height at `center`
    // (nearest vertex). Returns the number of vertices affected.
    int apply(Mesh& mesh, Vec3 center) const;

    // Apply the current brush to a real tile's source MCNK height grids (MCVT)
    // in place -- the sculpt path for a loaded ADT tile, vs. apply() which edits
    // the procedural render mesh. blockX/blockY are the tile's WDT indices. For
    // Flatten, level toward `center.z` (the picked surface point). Returns the
    // number of height samples affected. Re-mesh the tile afterwards to see it.
    int applyChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                    Vec3 center) const;

    bool enabled() const { return enabled_; }

    // --- UI state (public for tests) ---
    bool  enabled_  = false;   // when on, the host sculpts on terrain drag
    int   mode_     = 0;       // 0 Raise, 1 Lower, 2 Flatten
    float radius_   = 20.0f;
    float strength_ = 2.0f;
    int   falloff_  = 0;       // index into the editing Falloff profiles

private:
    // Build the brush from the current UI state, centred at `center`.
    Brush makeBrush(Vec3 center) const;
};

} // namespace wf::editor
