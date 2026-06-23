#pragma once
// ---------------------------------------------------------------------------
// vmap: parse a mangos-zero VMAP model file (`.vmo`, WorldModel) into collision
// triangle meshes, for rendering the *exact* server collision geometry as a
// wireframe (DebugCategory::Collision) -- the ground truth behind LoS/`.debug
// vis collision`. Magic + chunk layout per mangos-zero src/game/vmap/
// (VMAP_MAGIC "VMAP_4.0"; per group: AABox + flags + id, then self-describing
// "VERT" and "TRIM" chunks).
//
// The collision triangles live in the model's VERT/TRIM chunks, which are
// length-prefixed and self-describing, so we locate them by scan (validating
// each candidate's size) instead of walking the variable-size BIH trees.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>

#include "math.hpp"
#include "terrain.hpp"    // Mesh
#include "debugdraw.hpp"

namespace wf {

struct VmapGroup {
    Vec3     bmin, bmax;          // group AABB
    uint32_t flags = 0;          // MOGP flags
    uint32_t wmoId = 0;
    std::vector<Vec3>                    vertices;   // MOVT-equivalent
    std::vector<std::array<uint32_t, 3>> triangles;  // index triples
};

struct VmapModel {
    uint32_t rootWmoId = 0;
    std::vector<VmapGroup> groups;
};

// Parse a WorldModel (.vmo). Throws std::runtime_error on a bad magic.
VmapModel parseWorldModel(const std::vector<uint8_t>& buf);

// One group's collision geometry as a renderable Mesh (flat normals).
Mesh vmapGroupToMesh(const VmapGroup& group);

// Draw every group's collision triangle edges into a DebugDraw.
void addCollision(DebugDraw& dd, const VmapModel& model, Rgba color,
                  DebugCategory cat = DebugCategory::Collision);

} // namespace wf
