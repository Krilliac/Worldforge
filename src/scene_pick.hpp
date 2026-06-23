#pragma once
// ---------------------------------------------------------------------------
// Scene picking: hit-test a viewport ray against a fully-populated TileScene --
// the terrain chunks, the placed M2 doodads, and the WMO geometry -- returning
// whichever triangle is nearest along the ray. This is the static-world analog
// of pickEntity (which handles the dynamic creature/player stream): clicking a
// building or a doodad in the world selects it per-triangle, not by a box.
// ---------------------------------------------------------------------------
#include <cstdint>

#include "asset_loader.hpp"   // TileScene
#include "gizmo.hpp"          // Ray

namespace wf {

struct ScenePick {
    enum class Kind { None, Terrain, Doodad, Wmo };
    Kind     kind     = Kind::None;
    size_t   index    = 0;     // terrain chunk / doodad instance / wmo instance index
    uint32_t uniqueId = 0;     // WMO placement id (Wmo only)
    Vec3     point;            // world-space hit
    float    distance = 0.0f;
    bool hit() const { return kind != Kind::None; }
};

// Nearest hit across terrain + doodads + WMOs (None if the ray misses the tile).
ScenePick pickScene(const Ray& r, const TileScene& scene);

} // namespace wf
