#pragma once
// ---------------------------------------------------------------------------
// Picking: turn a click in the viewport into a world-space ray and hit-test it
// against the things the editor can select -- live entities (NPCs/players/
// objects from the WorldView) and the terrain mesh. The ray is built straight
// from the camera basis + FOV (no matrix inverse needed), so it stays exact and
// is trivially unit-testable. pick() returns whichever is nearest, so clicking
// an NPC standing on the ground selects the NPC, and clicking open ground
// returns the terrain hit point (for placing/spawning).
// ---------------------------------------------------------------------------
#include <cstdint>

#include "math.hpp"
#include "terrain.hpp"     // Mesh
#include "gizmo.hpp"       // Ray, rayTriangle, pickMesh (shared picking math)
#include "world_view.hpp"

namespace wf {

// Build the world-space ray through viewport pixel (sx, sy) for a camera with
// the given basis (forward/right/up, all unit) and vertical FOV. (sx,sy) are in
// pixels with the origin at the image's top-left; (width,height) is its size.
Ray screenRay(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
              double fovYDeg, double aspect, float sx, float sy, float width, float height);

// Ray vs sphere: nearest positive hit distance along the ray, or < 0 for a miss.
// (Ray-triangle / ray-mesh come from gizmo.hpp: rayTriangle / pickMesh.)
float raySphere(const Ray& r, const Vec3& center, float radius);

// What a click resolved to.
struct PickResult {
    enum class Kind { None, Entity, Terrain };
    Kind     kind     = Kind::None;
    uint64_t guid     = 0;          // valid for Kind::Entity
    Vec3     point;                 // world-space hit point
    float    distance = 0.0f;       // along the ray
    bool hit() const { return kind != Kind::None; }
};

// The selectable radius for an entity: its reported bounds (or `fallback` when
// unknown), plus `pad` of click forgiveness so small objects stay clickable.
float pickRadius(const EntityState& s, float pad = 0.5f, float fallback = 2.0f);

// Nearest entity the ray hits (each a sphere of pickRadius()), or a None result.
// guid + distance + point filled on a hit.
PickResult pickEntity(const Ray& r, const WorldView& view, float pad = 0.5f, float fallback = 2.0f);
// Nearest terrain triangle the ray hits.
PickResult pickTerrain(const Ray& r, const Mesh& terrain);

// Nearest of entity / terrain (entities win ties, since their spheres sit in
// front of the ground they stand on).
PickResult pick(const Ray& r, const WorldView& view, const Mesh& terrain,
                float pad = 0.5f, float fallback = 2.0f);

} // namespace wf
