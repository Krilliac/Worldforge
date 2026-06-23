#pragma once
// ---------------------------------------------------------------------------
// m2_render: turn a parsed M2 model + a bone pose into a renderable TexMesh.
// Skinning blends up to 4 bone matrices per vertex by weight (the same weights/
// indices parsed into M2Vertex), transforming position + normal -- so an M2
// plays its animation when fed successive computePose() results. With an
// identity / empty pose it yields the static bind-pose mesh. UVs come straight
// from the model, so the textured rasteriser can shade it with its BLP.
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"
#include "m2.hpp"
#include "raster.hpp"   // TexMesh

namespace wf {

// Skin view 0 of `model` with `bonePose` (world-space bone matrices from
// computePose; pass an empty vector for the static bind pose). Returns a TexMesh
// in model-local space; the caller applies the placement transform.
TexMesh skinM2(const M2Model& model, const std::vector<Mat4>& bonePose);

// Convenience: sample the model's animation `animIndex` at time `tMs` and skin.
// (Builds the bones for that sequence and composes the pose.)
TexMesh poseM2(const M2Model& model, const M2Animation& anim, int animIndex, uint32_t tMs);

} // namespace wf
