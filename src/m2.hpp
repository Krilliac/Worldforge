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
#include "client_version.hpp"

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

// Static (bind-pose) attachment point: a hardpoint on a bone where items, weapon
// trails, etc. are anchored. Only the leading id/bone/position fields are kept;
// the per-record AnimationBlock (animate-attached flag) is skipped.
struct M2Attachment {
    uint32_t id   = 0;     // attachment slot id (ItemVisuals / hardcoded slots)
    uint32_t bone = 0;     // bone index this attaches to
    Vec3     position;     // offset from the bone, in model space
};

// Static camera definition (cinematics / portrait). Only the leading scalar
// fields + the static position are kept; the per-record position/target/roll
// AnimationBlocks are skipped.
struct M2Camera {
    uint32_t type     = 0; // 0 = portrait, 1 = characterInfo, -1 = unused
    float    fov      = 0;  // vertical field of view (radians)
    float    farClip  = 0;
    float    nearClip = 0;
    Vec3     position;     // static eye position
};

// Static light definition. Only the leading type/bone/position fields are kept;
// the per-record colour/intensity/attenuation AnimationBlocks are skipped.
struct M2Light {
    uint32_t type = 0;     // 0 = directional, 1 = point (vanilla stores uint16)
    int32_t  bone = -1;    // parent bone index, -1 if attached to the model root
    Vec3     position;     // light position in bone space
};

// Static ribbon-emitter definition (weapon trails, etc.). Only the leading
// id/bone/position fields are kept; the embedded colour/alpha/height
// AnimationBlocks that follow are skipped via the record stride.
struct M2RibbonEmitter {
    uint32_t id   = 0;     // emitter id (-1 in many files)
    int32_t  bone = 0;     // bone the emitter rides
    Vec3     position;     // offset from the bone, in model space
};

// Static particle-emitter definition. Only the leading id/bone/position fields
// are kept; the large block of emission AnimationBlocks that follow are skipped
// via the record stride.
struct M2ParticleEmitter {
    uint32_t id   = 0;     // emitter id (-1 in many files)
    int32_t  bone = 0;     // bone the emitter rides
    Vec3     position;     // offset from the bone, in model space
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

    // Static fields of the attachment / camera / light arrays. Parsed additively
    // and guarded: a model lacking these arrays leaves them empty.
    std::vector<M2Attachment> attachments;
    std::vector<M2Camera>     cameras;
    std::vector<M2Light>      lights;

    // Static fields of the ribbon / particle emitter arrays. Parsed additively
    // and guarded: a model lacking these arrays leaves them empty.
    std::vector<M2RibbonEmitter>   ribbonEmitters;
    std::vector<M2ParticleEmitter> particleEmitters;

    // Resolve a triangle index to a global vertex index.
    uint32_t resolveVertex(uint16_t triIndex) const {
        return vertexLookup[triIndex];
    }
};

// Parse an M2 buffer. Throws std::runtime_error on a non-MD20 / malformed file.
// Parse a vanilla MD20 (M2) model. `profile` gates which on-disk M2 version this
// client understands (vanilla 1.12.1 = 0x100); a newer model fails loud. Defaults
// to the vanilla profile so existing callers are unaffected.
M2Model parseM2(const std::vector<uint8_t>& buf,
                const ClientProfile& profile = vanilla1121Profile());

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

// ---- material animation (color / alpha / texture-weight) ----
// Color animations carry an animated RGB tint and a separate animated alpha;
// texture-weight (a.k.a. transparency) animations carry an animated scalar
// opacity. Both ride the same 28-byte AnimationBlock / M2Track machinery as
// bones (RawChannel<T>) and may be driven by a global sequence. Alpha/weight
// are stored on disk as fixed16 (int16, value/32767) and decoded to float here.
struct M2ColorRaw {                 // ModelColorDef (56 bytes = 2 AnimationBlocks)
    RawChannel<Vec3>  rgb;          // value type = Vec3 (linear 0..1)
    RawChannel<float> alpha;        // value type = fixed16 -> float (0..1)
};
struct M2TextureWeightRaw {         // ModelTransDef (28 bytes = 1 AnimationBlock)
    RawChannel<float> weight;       // value type = fixed16 -> float (0..1)
};

struct M2Animation {
    std::vector<M2Sequence> sequences;
    std::vector<M2BoneRaw>  bones;

    // Material-level animation tracks + their lookup tables + the global
    // sequence duration table. Parsed additively; empty on models without them.
    std::vector<uint32_t>          globalSeqs;        // durations (ms) per global sequence
    std::vector<M2ColorRaw>        colors;            // ModelColorDef[]   (nColors @ 0x44)
    std::vector<M2TextureWeightRaw> textureWeights;   // ModelTransDef[]   (nTransparency @ 0x54)
    std::vector<uint16_t>          transparencyLookup; // transparency_lookup_table (@ 0x8C)
};

// Parse sequences + bones from an M2 buffer.
M2Animation parseM2Animation(const std::vector<uint8_t>& buf);

// Slice raw channels to one animation, producing pose-ready Bones (anim.hpp).
std::vector<Bone> buildBonesForAnimation(const M2Animation& anim, int animIndex);

// ---- material-animation sampling -------------------------------------------
// A sampled material tint: an RGB color and a combined alpha. The alpha already
// folds colorAlpha * textureWeight; the renderer additionally multiplies the
// per-texel BLP alpha to get final pixel alpha (texelAlpha*colorAlpha*weight).
struct M2Tint {
    Vec3  rgb   = {1, 1, 1};
    float alpha = 1.0f;
};

// Sample a color animation `colorIndex` (into anim.colors) at the given times.
// `animIndex` selects the per-animation keyframe range; `animTimeMs` is the
// time within that animation; `globalTimeMs` drives any global-sequence track.
// Returns {1,1,1,1} for an out-of-range index (no animation -> identity tint).
M2Tint sampleM2Color(const M2Animation& anim, int colorIndex,
                     int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs);

// Sample a texture-weight (transparency) animation. `weightIndex` may index the
// textureWeights array directly, or -- when the transparency lookup table is
// populated -- be redirected through it (resolved internally). Returns 1.0 for
// an out-of-range index.
float sampleM2TextureWeight(const M2Animation& anim, int weightIndex,
                            int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs);

// Combined per-submesh tint: color.rgb, alpha = colorAlpha * textureWeight.
// Pass the raw batch indices (colorIndex / textureWeightIndex, -1 if none).
M2Tint sampleM2Tint(const M2Animation& anim, int colorIndex, int weightIndex,
                    int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs);

} // namespace wf
