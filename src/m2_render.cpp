#include "m2_render.hpp"

#include "anim.hpp"

namespace wf {

namespace {
// Transform a position (w=1) by a column-major Mat4.
Vec3 xformPos(const Mat4& m, const Vec3& p) {
    Vec4 r = m * Vec4(p, 1.0f);
    return { r.x, r.y, r.z };
}
// Transform a direction (w=0).
Vec3 xformDir(const Mat4& m, const Vec3& d) {
    Vec4 r = m * Vec4(d, 0.0f);
    return { r.x, r.y, r.z };
}
} // namespace

TexMesh skinM2(const M2Model& model, const std::vector<Mat4>& pose) {
    TexMesh mesh;
    mesh.vertices.reserve(model.vertexLookup.size());

    for (uint16_t gi : model.vertexLookup) {
        if (gi >= model.vertices.size()) { mesh.vertices.push_back({}); continue; }
        const M2Vertex& v = model.vertices[gi];

        Vec3 pos = v.pos, nrm = v.normal;
        if (!pose.empty()) {
            // Blend up to 4 bones by weight (weights are 0..255, sum ~255).
            Vec3 bp{0,0,0}, bn{0,0,0};
            float wsum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                uint8_t w = v.boneWeights[i];
                if (!w) continue;
                uint8_t bi = v.boneIndices[i];
                if (bi >= pose.size()) continue;
                float fw = w / 255.0f;
                bp += xformPos(pose[bi], v.pos) * fw;
                bn += xformDir(pose[bi], v.normal) * fw;
                wsum += fw;
            }
            if (wsum > 1e-4f) { pos = bp * (1.0f / wsum); nrm = bn * (1.0f / wsum); }
        }
        mesh.vertices.push_back({ pos, normalize(nrm), v.uv });
    }

    mesh.indices.reserve(model.triangles.size());
    for (uint16_t idx : model.triangles)
        if (idx < mesh.vertices.size()) mesh.indices.push_back(idx);
    mesh.indices.resize(mesh.indices.size() - (mesh.indices.size() % 3));
    return mesh;
}

TexMesh skinM2Geosets(const M2Model& model, const std::vector<Mat4>& pose,
                      const std::unordered_map<uint16_t, uint16_t>& chosen) {
    TexMesh mesh = skinM2(model, pose);         // full skinned vertices + all indices
    if (model.submeshes.empty()) return mesh;   // no geosets: draw everything

    // Rebuild the index list from only the chosen submeshes' triangle ranges.
    std::vector<uint32_t> sel = selectGeosets(model.submeshes, chosen);
    std::vector<uint32_t> idx;
    for (uint32_t si : sel) {
        const M2Submesh& s = model.submeshes[si];
        for (uint32_t k = 0; k < s.indexCount; ++k) {
            uint32_t t = static_cast<uint32_t>(s.indexStart) + k;
            if (t >= model.triangles.size()) break;
            uint16_t vi = model.triangles[t];
            if (vi < mesh.vertices.size()) idx.push_back(vi);
        }
    }
    idx.resize(idx.size() - (idx.size() % 3));
    mesh.indices = std::move(idx);
    return mesh;
}

TexMesh poseM2(const M2Model& model, const M2Animation& anim, int animIndex, uint32_t tMs) {
    if (animIndex < 0 || animIndex >= (int)anim.sequences.size() || anim.bones.empty())
        return skinM2(model, {});                       // static bind pose
    std::vector<Bone> bones = buildBonesForAnimation(anim, animIndex);
    std::vector<Mat4> pose = computePose(bones, tMs);
    return skinM2(model, pose);
}

M2Tint submeshTint(const M2Animation& anim, int colorIndex, int weightIndex,
                   int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs) {
    // Delegate to the material-animation sampler: color RGB * (colorAlpha *
    // textureWeight). The per-texel BLP alpha is multiplied later in the
    // rasteriser, completing texelAlpha * colorAlpha * textureWeight.
    return sampleM2Tint(anim, colorIndex, weightIndex,
                        animIndex, animTimeMs, globalTimeMs);
}

} // namespace wf
