#include "wmo_render.hpp"

#include <algorithm>
#include <map>

namespace wf {

std::vector<WmoRenderPart> wmoRenderParts(const WmoModel& wmo) {
    // Accumulate triangles per material (flat -- one vertex per corner, so no
    // shared-index bookkeeping across materials/groups).
    std::map<uint8_t, TexMesh> byMaterial;

    for (const WmoGroup& g : wmo.groups) {
        const size_t triCount = g.indices.size() / 3;
        for (size_t t = 0; t < triCount; ++t) {
            uint8_t mat = t < g.triMaterial.size() ? g.triMaterial[t] : 0;
            TexMesh& tm = byMaterial[mat];
            for (int k = 0; k < 3; ++k) {
                uint16_t vi = g.indices[t * 3 + k];
                if (vi >= g.vertices.size()) continue;
                Vec3 p  = g.vertices[vi];
                Vec3 n  = vi < g.normals.size() ? g.normals[vi] : Vec3{0, 0, 1};
                Vec2 uv = vi < g.uvs.size()     ? g.uvs[vi]     : Vec2{0, 0};
                tm.indices.push_back(static_cast<uint32_t>(tm.vertices.size()));
                tm.vertices.push_back(TexVertex{ p, n, uv });
            }
        }
    }

    std::vector<WmoRenderPart> parts;
    parts.reserve(byMaterial.size());
    for (auto& kv : byMaterial) {
        WmoRenderPart part;
        part.mesh = std::move(kv.second);
        if (kv.first < wmo.root.materials.size()) {
            const WmoMaterial& m = wmo.root.materials[kv.first];
            part.texture   = m.diffuseTexture;
            part.blendMode = m.blendMode;
            part.flags     = m.flags;
        }
        parts.push_back(std::move(part));
    }
    // Opaque / alpha-test first, alpha-blended (>=2) last (painter's order).
    std::stable_sort(parts.begin(), parts.end(),
                     [](const WmoRenderPart& a, const WmoRenderPart& b){
                         return (a.blendMode >= 2 ? 1 : 0) < (b.blendMode >= 2 ? 1 : 0);
                     });
    return parts;
}

} // namespace wf
