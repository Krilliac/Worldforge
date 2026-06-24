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

// ---- per-submesh material tint (color/alpha + texture-weight animation) -----
// A posed model's submeshes carry, in the embedded skin profile's batch records,
// a colorIndex (-> ModelColorDef[]) and a textureWeightIndex (-> ModelTransDef[],
// possibly via the transparency lookup) that drive the material's animated RGB
// tint and overall opacity. Parsing those batch records is a later step; until
// then the renderer supplies the indices it has resolved. This evaluates them at
// a frame time and yields the constant-per-frame tint to modulate the textured
// triangles with:  finalRGB = texelRGB * tint.rgb,
//                  finalAlpha = texelAlpha * tint.alpha   (tint.alpha already
// folds colorAlpha * textureWeight). With colorIndex < 0 and weightIndex < 0
// this returns the identity tint {1,1,1, 1}, leaving the texel untouched.
//
// `animIndex` selects the per-animation keyframe range; `animTimeMs` is the time
// within that animation; `globalTimeMs` drives any global-sequence track (e.g. a
// flickering torch whose alpha pulses independently of the Stand animation).
M2Tint submeshTint(const M2Animation& anim, int colorIndex, int weightIndex,
                   int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs);

} // namespace wf
