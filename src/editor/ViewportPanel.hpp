#pragma once
// ---------------------------------------------------------------------------
// ViewportPanel: the embedded 3D view. It renders the scene (terrain mesh +
// debug overlay) on the CPU into an internal Image via the software rasteriser,
// then shows that image inside an ImGui panel and runs ImGuizmo over the
// selected object. The same image is what a GPU backend would blit -- here it is
// a registered software texture, so the whole viewport works headlessly.
// ---------------------------------------------------------------------------
#include "imgui.h"

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

class ViewportPanel {
public:
    Camera camera;

    explicit ViewportPanel(int w = 900, int h = 560)
        : width_(w), height_(h), scene_(w, h), fb_(w, h) {}

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

    // Resize the internal render target so the scene re-renders at (w,h). Called
    // when the docked Viewport panel changes size, so the 3D view fills its pane
    // and keeps the correct aspect ratio instead of stretching a fixed image.
    void resize(int w, int h);

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

    const Image& scene() const { return scene_; }
    int width()  const { return width_; }
    int height() const { return height_; }

private:
    // Realise a pending resize() request: reallocate scene_/fb_ to the requested
    // size so the image, width_/height_ and framebuffer stay consistent for the
    // whole frame. Called at the top of each render() overload (see resize()).
    void applyPendingResize();

    int width_, height_;
    int pendingW_ = 0, pendingH_ = 0;   // requested size; applied at next render()
    Image       scene_;
    Framebuffer fb_;
    PickResult  lastPick_;
    WorldPick   lastWorldPick_;

    // Build the world-space ray for a viewport pixel (shared by both pick paths).
    Ray rayAt(float localX, float localY) const;
};

} // namespace wf::editor
