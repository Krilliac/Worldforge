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
#include "world_view.hpp"

namespace wf {

struct Ray {
    Vec3 origin;
    Vec3 dir;     // normalised
};

// Build the world-space ray through viewport pixel (sx, sy) for a camera with
// the given basis (forward/right/up, all unit) and vertical FOV. (sx,sy) are in
// pixels with the origin at the image's top-left; (width,height) is its size.
Ray screenRay(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
              double fovYDeg, double aspect, float sx, float sy, float width, float height);

// Ray vs sphere: nearest positive hit distance along the ray, or < 0 for a miss.
float raySphere(const Ray& r, const Vec3& center, float radius);
// Ray vs triangle (Moller-Trumbore): hit distance, or < 0 for a miss/parallel.
float rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c);

// What a click resolved to.
struct PickResult {
    enum class Kind { None, Entity, Terrain };
    Kind     kind     = Kind::None;
    uint64_t guid     = 0;          // valid for Kind::Entity
    Vec3     point;                 // world-space hit point
    float    distance = 0.0f;       // along the ray
    bool hit() const { return kind != Kind::None; }
};

// Nearest entity the ray hits (treating each as a sphere of `radius`), or a
// None result. guid + distance + point filled on a hit.
PickResult pickEntity(const Ray& r, const WorldView& view, float radius = 3.0f);
// Nearest terrain triangle the ray hits.
PickResult pickTerrain(const Ray& r, const Mesh& terrain);

// Nearest of entity / terrain (entities win ties, since their spheres sit in
// front of the ground they stand on).
PickResult pick(const Ray& r, const WorldView& view, const Mesh& terrain,
                float entityRadius = 3.0f);

} // namespace wf
