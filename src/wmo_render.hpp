#pragma once
// ---------------------------------------------------------------------------
// Build renderable, textured geometry from a parsed WMO. Group triangles are
// grouped by their material (MOPY material id) into TexMeshes carrying the
// position/normal/UV the textured rasteriser needs, each tagged with the root
// material's diffuse texture name. The asset loader resolves those names to BLPs
// and drops the parts into the TileScene's instance list, so a building renders
// shaded like the doodads. Pure geometry -> mesh, unit-tested.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "wmo.hpp"
#include "raster.hpp"   // TexMesh

namespace wf {

struct WmoRenderPart {
    TexMesh     mesh;       // one material's triangles (position + normal + UV)
    std::string texture;    // diffuse BLP name ("" -> caller uses a fallback)
};

// Split a WMO's group geometry into one TexMesh per material, resolving each to
// its diffuse texture name. Triangles whose material id is out of range get an
// empty texture name (fallback).
std::vector<WmoRenderPart> wmoRenderParts(const WmoModel& wmo);

} // namespace wf
