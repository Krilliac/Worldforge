#include "modelmesh.hpp"

namespace wf {

Mesh m2ToMesh(const M2Model& model) {
    Mesh mesh;
    // The view selects global vertices through vertexLookup; build a compact
    // vertex buffer over exactly those, and remap triangles onto it.
    mesh.vertices.reserve(model.vertexLookup.size());
    for (uint16_t gi : model.vertexLookup) {
        if (gi < model.vertices.size()) {
            const M2Vertex& v = model.vertices[gi];
            mesh.vertices.push_back({ v.pos, v.normal });
        } else {
            mesh.vertices.push_back({ Vec3{0, 0, 0}, Vec3{0, 0, 1} });
        }
    }
    mesh.indices.reserve(model.triangles.size());
    for (uint16_t idx : model.triangles) {
        if (idx < mesh.vertices.size()) mesh.indices.push_back(idx);
    }
    // Drop a trailing partial triangle so the index count is a multiple of 3.
    mesh.indices.resize(mesh.indices.size() - (mesh.indices.size() % 3));
    return mesh;
}

Mesh wmoGroupToMesh(const WmoGroup& group) {
    Mesh mesh;
    mesh.vertices.reserve(group.vertices.size());
    for (size_t i = 0; i < group.vertices.size(); ++i) {
        Vec3 n = (i < group.normals.size()) ? group.normals[i] : Vec3{0, 0, 1};
        mesh.vertices.push_back({ group.vertices[i], n });
    }
    mesh.indices.reserve(group.indices.size());
    for (uint16_t idx : group.indices) {
        if (idx < mesh.vertices.size()) mesh.indices.push_back(idx);
    }
    mesh.indices.resize(mesh.indices.size() - (mesh.indices.size() % 3));
    return mesh;
}

TexMesh wmoGroupToTexMesh(const WmoGroup& group) {
    TexMesh mesh;
    mesh.vertices.reserve(group.vertices.size());
    for (size_t i = 0; i < group.vertices.size(); ++i) {
        Vec3 n  = (i < group.normals.size()) ? group.normals[i] : Vec3{0, 0, 1};
        Vec2 uv = (i < group.uvs.size())     ? group.uvs[i]     : Vec2{0, 0};
        mesh.vertices.push_back({ group.vertices[i], n, uv });
    }
    mesh.indices.reserve(group.indices.size());
    for (uint16_t idx : group.indices)
        if (idx < mesh.vertices.size()) mesh.indices.push_back(idx);
    mesh.indices.resize(mesh.indices.size() - (mesh.indices.size() % 3));
    return mesh;
}

} // namespace wf
