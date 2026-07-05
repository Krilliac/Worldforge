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
                // MOCV baked interior lighting, if present (else white = no tint).
                Rgba col = vi < g.vertexColors.size() ? g.vertexColors[vi]
                                                      : Rgba{255, 255, 255, 255};
                tm.indices.push_back(static_cast<uint32_t>(tm.vertices.size()));
                tm.vertices.push_back(TexVertex{ p, n, uv, col });
            }
        }
    }

    std::vector<WmoRenderPart> parts;
    parts.reserve(byMaterial.size());
    for (auto& kv : byMaterial) {
        WmoRenderPart part;
        part.mesh = std::move(kv.second);
        // MOCV present if any vertex carries a non-white baked colour (set above).
        for (const TexVertex& v : part.mesh.vertices)
            if (v.color.r != 255 || v.color.g != 255 || v.color.b != 255) {
                part.hasVertexColors = true; break;
            }
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

Mesh buildWmoLiquidMesh(const WmoLiquid& liq) {
    Mesh mesh;
    if (!liq.present || liq.xverts == 0 || liq.yverts == 0 ||
        liq.xtiles == 0 || liq.ytiles == 0 ||
        liq.heights.empty() || liq.tileFlags.empty())
        return mesh;

    const uint32_t cols = liq.xverts;   // expected = xtiles + 1
    const uint32_t rows = liq.yverts;   // expected = ytiles + 1
    const float    U    = 1.0f;         // one WMO-local unit per liquid tile

    auto vertIdx = [cols](uint32_t i, uint32_t j) { return j * cols + i; };

    // Vertex grid: position = baseCoords + (i*U, j*U, 0), Z replaced by the
    // stored surface height. Flat surface -> +Z normals.
    mesh.vertices.resize(static_cast<size_t>(cols) * rows);
    for (uint32_t j = 0; j < rows; ++j) {
        for (uint32_t i = 0; i < cols; ++i) {
            size_t idx = vertIdx(i, j);
            float  z   = idx < liq.heights.size() ? liq.heights[idx] : liq.baseCoords.z;
            mesh.vertices[idx].position = {
                liq.baseCoords.x + i * U,
                liq.baseCoords.y + j * U,
                z
            };
            mesh.vertices[idx].normal = { 0.0f, 0.0f, 1.0f };
        }
    }

    // Two triangles per rendered tile; skip tiles whose flag low nibble is 0xF
    // ("don't render", matching MCLQ). Guard every corner against the grid.
    mesh.indices.reserve(static_cast<size_t>(liq.xtiles) * liq.ytiles * 2 * 3);
    for (uint32_t tj = 0; tj < liq.ytiles; ++tj) {
        for (uint32_t ti = 0; ti < liq.xtiles; ++ti) {
            size_t flagIdx = static_cast<size_t>(tj) * liq.xtiles + ti;
            if (flagIdx >= liq.tileFlags.size()) continue;
            if ((liq.tileFlags[flagIdx] & 0x0F) == 0x0F) continue;
            if (ti + 1 >= cols || tj + 1 >= rows) continue;
            uint32_t TL = static_cast<uint32_t>(vertIdx(ti,     tj));
            uint32_t TR = static_cast<uint32_t>(vertIdx(ti + 1, tj));
            uint32_t BL = static_cast<uint32_t>(vertIdx(ti,     tj + 1));
            uint32_t BR = static_cast<uint32_t>(vertIdx(ti + 1, tj + 1));
            mesh.indices.insert(mesh.indices.end(), { TL, TR, BR });
            mesh.indices.insert(mesh.indices.end(), { TL, BR, BL });
        }
    }
    return mesh;
}

} // namespace wf
