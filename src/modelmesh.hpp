#pragma once
// ---------------------------------------------------------------------------
// modelmesh: convert parsed M2 / WMO geometry into the renderer's generic Mesh
// (terrain.hpp), so doodads and world objects can be drawn -- or, for the debug
// overlay, turned into DoodadWire wireframe via DebugDraw::wireframe. Output is
// in the model's own local space; the caller applies the MDDF/MODF placement
// transform. Pure data, unit-tested headless.
// ---------------------------------------------------------------------------
#include "terrain.hpp"   // Mesh / Vertex
#include "m2.hpp"
#include "wmo.hpp"

namespace wf {

// M2 view-0 static mesh -> Mesh. Resolves the view's vertexLookup so `triangles`
// (which index the lookup) become direct Mesh indices over a compact vertex set.
Mesh m2ToMesh(const M2Model& model);

// WMO group (MOVT/MONR/MOVI) -> Mesh, 1:1 with the group's vertex/index arrays.
Mesh wmoGroupToMesh(const WmoGroup& group);

} // namespace wf
