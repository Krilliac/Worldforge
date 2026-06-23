#pragma once
// ---------------------------------------------------------------------------
// M2 (MD20) model parser for vanilla 1.12.1 (version 0x100). Header offsets
// verified against getMaNGOS' vanilla layout; vanilla embeds its view/skin
// profiles in the file (external .skin files are WotLK+). This module reads the
// static renderable mesh: global vertices, textures, and view 0's vertex
// lookup + triangle list + submeshes. Bones and animation tracks are handled
// separately (anim.hpp), because the vanilla M2Track layout differs from later
// versions and warrants its own verification.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "math.hpp"
#include "anim.hpp"

namespace wf {

struct M2Vertex {
    Vec3    pos;
    uint8_t boneWeights[4] = {0, 0, 0, 0};
    uint8_t boneIndices[4] = {0, 0, 0, 0};
    Vec3    normal;
    Vec2    uv;       // first texture-coordinate set
};

struct M2Submesh {       // a draw range within view 0
    uint16_t id          = 0;
    uint16_t vertexStart = 0;
    uint16_t vertexCount = 0;
    uint16_t indexStart  = 0;   // into the triangle index list
    uint16_t indexCount  = 0;
};

struct M2Model {
    uint32_t version = 0;
    std::string name;

    std::vector<M2Vertex>    vertices;       // global vertex pool
    std::vector<std::string> textures;       // texture filenames (type 0 = hardcoded path)
    std::vector<uint32_t>    textureTypes;   // parallel to `textures`

    // View 0 (LOD 0): the lookup selects which global vertices this view uses,
    // and `triangles` indexes into that lookup (3 per triangle).
    std::vector<uint16_t>  vertexLookup;
    std::vector<uint16_t>  triangles;
    std::vector<M2Submesh> submeshes;

    // Resolve a triangle index to a global vertex index.
    uint32_t resolveVertex(uint16_t triIndex) const {
        return vertexLookup[triIndex];
    }
};

// Parse an M2 buffer. Throws std::runtime_error on a non-MD20 / malformed file.
M2Model parseM2(const std::vector<uint8_t>& buf);

// ---- animation (sequences + bones) ----
// Vanilla M2Track uses per-animation interpolation ranges into single
// timestamp/value arrays (WotLK+ switched to nested per-animation arrays).
struct M2Sequence {
    uint16_t id     = 0;   // AnimationData.dbc id
    uint16_t subId  = 0;
    uint32_t length = 0;   // milliseconds
    uint32_t flags  = 0;
};

template <typename T>
struct RawChannel {
    uint16_t interp = 1;
    int16_t  globalSeq = -1;
    std::vector<std::pair<uint32_t, uint32_t>> ranges; // per-animation [first,last] into times/values
    std::vector<uint32_t> times;
    std::vector<T>        values;
};

struct M2BoneRaw {
    int  parent = -1;
    Vec3 pivot;
    RawChannel<Vec3> translation;
    RawChannel<Quat> rotation;
    RawChannel<Vec3> scale;
};

struct M2Animation {
    std::vector<M2Sequence> sequences;
    std::vector<M2BoneRaw>  bones;
};

// Parse sequences + bones from an M2 buffer.
M2Animation parseM2Animation(const std::vector<uint8_t>& buf);

// Slice raw channels to one animation, producing pose-ready Bones (anim.hpp).
std::vector<Bone> buildBonesForAnimation(const M2Animation& anim, int animIndex);

} // namespace wf
