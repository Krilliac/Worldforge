#pragma once
// Procedural assets for the model-viewer demo/tests: a 2-bone "creature" (a
// tapered box whose upper half is skinned to a bone that sways over the
// sequence) + a checker texture. Exercises the real parse-free path
// M2Animation -> buildBonesForAnimation -> computePose -> skinM2 -> rasterTexMesh.
#include <cmath>
#include <vector>

#include "image.hpp"
#include "math.hpp"
#include "m2.hpp"

namespace wf::editor {

inline Quat axisAngleDeg(Vec3 axis, float deg) {
    float a = deg * 3.14159265f / 180.0f * 0.5f;
    Vec3 n = normalize(axis);
    float s = std::sin(a);
    return Quat{ n.x * s, n.y * s, n.z * s, std::cos(a) };
}

inline M2Model makeDemoModel() {
    M2Model m;
    const Vec3 P[8] = {
        {-0.5f,-0.5f,0}, {0.5f,-0.5f,0}, {0.5f,0.5f,0}, {-0.5f,0.5f,0},   // lower -> bone 0
        {-0.4f,-0.4f,2}, {0.4f,-0.4f,2}, {0.4f,0.4f,2}, {-0.4f,0.4f,2},   // upper -> bone 1
    };
    m.vertices.resize(8);
    for (int i = 0; i < 8; ++i) {
        M2Vertex v;
        v.pos = P[i];
        v.normal = normalize(P[i] - Vec3{0,0,1});
        v.uv = { P[i].x + 0.5f, P[i].z * 0.5f };
        uint8_t bone = (i < 4) ? 0 : 1;
        v.boneWeights[0] = 255; v.boneIndices[0] = bone;
        m.vertices[i] = v;
    }
    m.vertexLookup = {0,1,2,3,4,5,6,7};
    m.triangles = {
        0,1,5, 0,5,4,  1,2,6, 1,6,5,  2,3,7, 2,7,6,  3,0,4, 3,4,7,   // sides
        4,5,6, 4,6,7,                                                 // top
        0,2,1, 0,3,2,                                                 // bottom
    };
    M2Submesh sm; sm.vertexCount = 8; sm.indexCount = (uint16_t)m.triangles.size();
    m.submeshes.push_back(sm);
    return m;
}

inline M2Animation makeDemoAnim() {
    M2Animation a;
    M2Sequence seq; seq.id = 0; seq.length = 1000;
    a.sequences.push_back(seq);

    M2BoneRaw root;  root.parent = -1; root.pivot = {0,0,0};
    M2BoneRaw sway;  sway.parent = 0;  sway.pivot = {0,0,1};
    // Rotation track: 0deg -> +35deg about X -> 0deg, over the 1s sequence.
    sway.rotation.interp = 1;
    sway.rotation.times  = { 0, 500, 1000 };
    sway.rotation.values = { Quat::identity(), axisAngleDeg({1,0,0}, 35.0f), Quat::identity() };
    sway.rotation.ranges = { { 0, 2 } };               // animation 0 uses keys 0..2
    a.bones = { root, sway };
    return a;
}

inline Image makeCheckerTexture(int n = 16) {
    Image t(n, n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            bool c = ((x / 2) + (y / 2)) & 1;
            t.at(x, y) = c ? Rgba{210,170,90,255} : Rgba{90,110,150,255};
        }
    return t;
}

} // namespace wf::editor
