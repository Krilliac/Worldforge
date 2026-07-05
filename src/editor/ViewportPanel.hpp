#pragma once
// ---------------------------------------------------------------------------
// ViewportPanel: the embedded 3D view. It renders the scene (terrain mesh +
// debug overlay) on the CPU into an internal Image via the software rasteriser,
// then shows that image inside an ImGui panel and runs ImGuizmo over the
// selected object. The same image is what a GPU backend would blit -- here it is
// a registered software texture, so the whole viewport works headlessly.
// ---------------------------------------------------------------------------
#include "imgui.h"

#include <functional>
#include <vector>

#include "image.hpp"
#include "math.hpp"
#include "terrain.hpp"     // Mesh
#include "raster.hpp"      // Framebuffer
#include "debugdraw.hpp"
#include "picking.hpp"     // Ray / PickResult / pick
#include "scene_pick.hpp"  // TileScene / WorldPick / pickWorld
#include "world_view.hpp"
#include "editor/Camera.hpp"
#include "editor/GizmoController.hpp"

namespace wf::editor {

// Cursor spec of the active tool, fed by the host each frame so the viewport's
// overlay pass can draw it: a ground-conforming circle for radius brushes
// (sculpt/paint) or the hovered MCNK square for chunk-scoped tools (holes,
// area id). Pure state -- geometry comes from the free functions below.
struct BrushCursor {
    enum class Shape { None, Circle, ChunkSquare };
    Shape shape = Shape::None;
    // Circle:
    Vec3  center;                    // world-space brush centre (the pick point)
    float radius     = 0.0f;         // world yards
    float innerRatio = 0.0f;         // inner full-strength ring fraction [0,1]
    // ChunkSquare:
    int   blockX = 0, blockY = 0;    // hovered tile (WDT indices)
    int   row = 0, col = 0;          // hovered MCNK row/col within the tile (0..15)
};

// Ground-conforming brush-ring geometry: `n` points evenly spaced around
// `center` at `radius` in the world XY plane, each dropped onto the terrain by
// sampling heightFn(x, y) (falls back to center.z with a null heightFn). Pure
// -- the overlay pass turns the loop into DebugDraw line segments. Empty for a
// degenerate radius or n < 3.
std::vector<Vec3> brushRingPoints(const Vec3& center, float radius,
                                  const std::function<float(float, float)>& heightFn,
                                  int n = 32);

// The four corners of MCNK (row, col) of tile (blockX, blockY) as a closed
// loop (NW -> SW -> SE -> NE in world axes), heights via heightFn (0 when
// null) -- the hovered-chunk square for chunk-scoped tools. Pure.
std::vector<Vec3> chunkSquarePoints(int blockX, int blockY, int row, int col,
                                    const std::function<float(float, float)>& heightFn);

class ViewportPanel {
public:
    Camera camera;

    explicit ViewportPanel(int w = 900, int h = 560)
        : width_(w), height_(h), displayW_(w), displayH_(h), scene_(w, h), fb_(w, h) {}

    // Render terrain + overlay into the internal scene image at the camera.
    // `light` supplies coloured ambient/diffuse (e.g. resolved from Light.dbc);
    // the default reproduces the legacy grey directional light exactly.
    void render(const Mesh& terrain, const DebugDraw& dd, const ShadeLight& light = {});

    // Render a real ADT tile instead of a bare mesh: textured terrain splat +
    // placed doodads/WMOs + translucent liquid, shaded by the tile's resolved
    // zone light (`scene.light`) with the sun direction taken from `light.dir`.
    // Same internal target / resize / overlay path as the Mesh overload.
    void render(const TileScene& scene, const DebugDraw& dd, const ShadeLight& light = {});

    // Render a streaming multi-tile world: a coarse WDL "far" mesh for the horizon
    // drawn first, then every full-resolution near tile (depth-tested, so real
    // terrain overwrites the coarse horizon where they overlap). Caller passes the
    // already-culled near tiles + the already-built (and culled) far mesh.
    void render(const std::vector<const TileScene*>& nearTiles, const Mesh& wdlFar,
                const DebugDraw& dd, const ShadeLight& light = {});

    // Zone atmosphere for the viewport backdrop. When valid, render() clears to
    // a sky gradient derived from the zone fog colour (linear 0..1, e.g.
    // LightingSample::fog) and runs a distance-fog post-pass over the geometry;
    // when invalid (the default) the legacy flat clear is used and no fog is
    // applied, so existing callers/tests are unchanged. The editor re-sets this
    // every frame alongside the ShadeLight so it tracks the day tick.
    void setAtmosphere(bool valid, const Vec3& fogColorLinear);

    // Resize the panel to (w,h) display pixels. The internal render target is
    // sized to w*renderScale x h*renderScale (see setRenderScale); the smaller
    // image is stretched to the full panel by the GPU (LINEAR), so the CPU
    // software rasteriser shades far fewer pixels. Aspect is preserved, so
    // picking/gizmo (which work in display coords) stay correct.
    void resize(int w, int h);

    // Internal-resolution factor in (0,1]: 1.0 = render at full panel resolution
    // (sharpest), lower = fewer pixels rasterised (faster, softer). The terrain
    // fill cost scales with pixel count, so this is the main viewport perf knob.
    // Clamped to [0.25, 1.0]; a change re-sizes the render target next frame.
    void  setRenderScale(float s);
    float renderScale() const { return renderScale_; }

    // ImGui panel: show the scene (sampled from `sceneTex`) and run the gizmo
    // over `selected` (may be null). Returns true while the gizmo is dragged.
    //
    // A left-click on the image (not a gizmo drag) picks under the cursor:
    //   * with `scene`     -> the unified pick (live entities + loaded tile):
    //                         an entity sets the WorldView selection, a static
    //                         object (terrain/doodad/WMO) is written to *sceneSel
    //                         (and the entity selection cleared);
    //   * else with `terrain` -> entity + terrain-mesh pick (lastPick()).
    bool draw(ImTextureID sceneTex, GizmoController& giz, Mat4* selected,
              WorldView* view = nullptr, const Mesh* terrain = nullptr,
              const TileScene* scene = nullptr, WorldPick* sceneSel = nullptr);

    // Hit-test viewport pixel (localX, localY) -- origin at the image's top-left.
    // Pure (no ImGui), tested directly without synthesising mouse input.
    PickResult pickAt(float localX, float localY,
                      const WorldView& view, const Mesh& terrain) const;
    // Unified pick (entities + the whole loaded tile) at a viewport pixel.
    WorldPick pickWorldAt(float localX, float localY,
                          const WorldView& view, const TileScene& scene) const;

    const PickResult& lastPick() const { return lastPick_; }
    const WorldPick&  lastWorldPick() const { return lastWorldPick_; }

    // --- brush cursor overlay ------------------------------------------------
    // The host sets the active tool's cursor spec each frame (Shape::None to
    // hide it); the overlay pass reads it back / appends its geometry.
    void setBrushCursor(const BrushCursor& c) { brushCursor_ = c; }
    const BrushCursor& brushCursor() const { return brushCursor_; }

    // Append the cursor's overlay geometry (ground-conforming ring -- plus an
    // inner ring when innerRatio > 0 -- or the hovered MCNK square) to `dd`,
    // drawn by the existing software-raster overlay pass (rasterDebug) with
    // everything else. heightFn samples terrain height at world (x, y).
    void appendBrushOverlay(DebugDraw& dd,
                            const std::function<float(float, float)>& heightFn) const;

    // Alt+drag radius resize: the horizontal mouse delta draw() saw this frame
    // while Alt+LMB-dragging over the image (0 otherwise). The host applies it
    // to the active tool's radius via resizeRadius().
    float altDragDeltaX() const { return altDragDx_; }
    // Pure mapping of an Alt-drag pixel delta onto a brush radius (0.25 yd per
    // pixel, clamped to [1, 200]).
    static float resizeRadius(float radius, float dragDeltaX);

    // Hovered MCNK square of tile (blockX, blockY) under a picked world point
    // (the existing pick path supplies the point); false when the point lies
    // outside that tile. Pure chunk-grid math.
    static bool chunkSquareAt(const Vec3& worldPoint, int blockX, int blockY,
                              int& rowOut, int& colOut);

    const Image& scene() const { return scene_; }
    int width()  const { return width_; }
    int height() const { return height_; }

private:
    // Realise a pending resize() request: reallocate scene_/fb_ to the requested
    // size so the image, width_/height_ and framebuffer stay consistent for the
    // whole frame. Called at the top of each render() overload (see resize()).
    void applyPendingResize();

    // Clear the target for a new frame: zone sky gradient when the atmosphere
    // is set, the legacy flat colour otherwise (shared by the render overloads).
    void clearBackdrop();
    // Fog post-pass over rendered geometry; no-op without a valid atmosphere.
    void fogPass();

    int width_, height_;                // internal RENDER (framebuffer) size
    int pendingW_ = 0, pendingH_ = 0;   // requested render size; applied at next render()
    int displayW_ = 900, displayH_ = 560; // panel size the scene is stretched to
    float renderScale_ = 1.0f;          // render size = display size * this
    bool skyValid_ = false;             // atmosphere set this frame?
    Rgba skyTop_{ 18, 20, 28, 255 };    // gradient top (derived from zone fog)
    Rgba skyHorizon_{ 18, 20, 28, 255 };// gradient horizon == fog colour
    Image       scene_;
    Framebuffer fb_;
    PickResult  lastPick_;
    WorldPick   lastWorldPick_;
    BrushCursor brushCursor_;           // active tool's cursor spec (host-fed)
    float       altDragDx_ = 0.0f;      // Alt+LMB drag delta seen by draw()

    // Build the world-space ray for a viewport pixel (shared by both pick paths).
    Ray rayAt(float localX, float localY) const;
};

} // namespace wf::editor
