#pragma once
// ---------------------------------------------------------------------------
// WMO (World Map Object) parser, v17 (vanilla 1.12.1). A WMO is split into a
// root file (textures, materials, group table, doodad sets/placements) and N
// group files (the actual geometry). Layouts verified against wowdev.wiki WMO.
//
// Note: doodads inside a WMO are oriented with a quaternion in the WMO's local
// Z-up space -- unlike ADT/WDT placements which use Euler angles. Group vertices
// are already transformed relative to the WMO origin (0,0,0).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>
#include "math.hpp"
#include "image.hpp"
#include "client_version.hpp"

namespace wf {

// ---- root ----
struct WmoMaterial {
    uint32_t flags = 0;
    uint32_t shader = 0;
    uint32_t blendMode = 0;
    uint32_t diffuseNameOffset = 0;   // byte offset into the MOTX texture blob
    std::string diffuseTexture;       // resolved
};

struct WmoDoodadSet {
    std::string name;
    uint32_t firstInstance = 0;
    uint32_t numDoodads = 0;
};

struct WmoDoodad {                    // MODD entry
    uint32_t nameOffset = 0;          // into MODN
    Vec3     position;                // WMO local space (stored X,Z,-Y per wiki)
    Vec4     orientation;             // quaternion (x,y,z,w)
    float    scale = 1.0f;
    std::string modelName;            // resolved from MODN
};

struct WmoGroupInfo {                 // MOGI entry
    uint32_t flags = 0;
    Vec3     bboxMin, bboxMax;
    int32_t  nameOffset = -1;
    std::string name;
};

struct WmoLight {                     // MOLT entry (48 bytes), interior lighting
    uint8_t type = 0;                 // 0 omni, 1 spot, 2 direct, 3 ambient
    bool    useAttenuation = false;
    Vec3    color;                    // RGB 0..1, decoded from the BGRA CImVector
    Vec3    position;                 // WMO local space
    float   intensity  = 0.0f;
    float   attenStart = 0.0f;        // attenuation begin/end radii (4 trailing
    float   attenEnd   = 0.0f;        // SMOLight floats are version-specific, skipped)
};

struct WmoFog {                       // MOFG entry (48 bytes), SMOFog
    uint32_t flags = 0;
    Vec3     position;                // WMO local space
    float    smallerRadius = 0.0f;    // attenuation radii
    float    largerRadius  = 0.0f;
    float    end   = 0.0f;            // first fog: end distance + start scalar
    float    startScalar = 0.0f;
    Vec3     color;                   // RGB 0..1, decoded from the BGRA CImVector
};

struct WmoPortal {                    // MOPT entry (20 bytes), SMOPortal
    uint16_t startVertex = 0;         // first vertex in MOPV
    uint16_t count = 0;               // vertex count
    Vec3     normal;                  // portal plane normal
    float    planeDist = 0.0f;        // plane distance
};

// MOPR entry (8 bytes), SMOPortalRef: which group a portal connects, and which
// side of the portal plane that group is on. A group's MOGP header names a range
// [moprIndex, moprIndex+moprCount) into the root MOPR array -- its adjacency list
// for interior portal-based visibility (draw a connected group only if its portal
// is visible through the current one).
struct WmoPortalRef {
    uint16_t portalIndex = 0;         // index into MOPT (which portal)
    uint16_t groupIndex  = 0;         // the group on the far side of that portal
    int16_t  side        = 0;         // +1 / -1: which side of the plane the group is
};

struct WmoRoot {
    uint32_t version = 0;   // MVER (17 for vanilla..Cata); 0 if no MVER chunk
    uint32_t nTextures = 0, nGroups = 0, nPortals = 0, nLights = 0;
    uint32_t nDoodadNames = 0, nDoodadDefs = 0, nDoodadSets = 0;
    uint16_t flags = 0;
    Vec3     bboxMin, bboxMax;

    std::vector<std::string>  textures;       // split from MOTX
    std::vector<WmoMaterial>  materials;       // MOMT
    std::vector<WmoGroupInfo> groups;          // MOGI
    std::vector<WmoDoodadSet> doodadSets;      // MODS
    std::vector<WmoDoodad>    doodads;          // MODD
    std::vector<WmoLight>     lights;           // MOLT
    std::string               skybox;           // MOSB (skybox model path)
    std::vector<WmoFog>       fogs;             // MOFG
    std::vector<Vec3>         portalVertices;   // MOPV
    std::vector<WmoPortal>    portals;          // MOPT
    std::vector<WmoPortalRef> portalRefs;       // MOPR (group -> portal adjacency)
};

// Parse a WMO root file. `profile` gates the WMO version this client understands
// (vanilla..Cata = 17); a newer root fails loud. Defaults to the vanilla profile
// so existing callers are unaffected.
WmoRoot parseWmoRoot(const std::vector<uint8_t>& buf,
                     const ClientProfile& profile = vanilla1121Profile());

// ---- group ----
struct WmoBatch {                     // MOBA entry
    uint32_t startIndex = 0;          // into the index list (MOVI)
    uint16_t indexCount = 0;
    uint16_t minIndex = 0;
    uint16_t maxIndex = 0;
    uint8_t  flags = 0;
    uint8_t  materialId = 0;          // into root MOMT (0xFF = none)
};

struct WmoBspNode {                  // MOBN entry (16 bytes), T_BSP_NODE
    uint16_t flags = 0;              // plane type / leaf flags
    int16_t  negChild = -1;          // child node indices (-1 = none)
    int16_t  posChild = -1;
    uint16_t nFaces = 0;             // leaf: face count into MOBV
    uint32_t faceStart = 0;          // leaf: first face index into MOBV
    float    planeDist = 0.0f;       // split plane distance
};

struct WmoLiquid {                   // MLIQ chunk (SMOLiquid), interior water/lava
    uint32_t xverts = 0, yverts = 0, xtiles = 0, ytiles = 0;
    Vec3     baseCoords;             // corner position in WMO space
    uint16_t materialId = 0;
    std::vector<float>   heights;    // xverts*yverts liquid surface heights
    std::vector<uint8_t> tileFlags;  // xtiles*ytiles render flags
    bool present = false;
};

struct WmoGroup {
    uint32_t flags = 0;
    Vec3     bboxMin, bboxMax;
    uint16_t moprIndex = 0;           // MOGP: first MOPR ref for this group
    uint16_t moprCount = 0;           // MOGP: number of MOPR refs (portal adjacency)

    std::vector<Vec3>     vertices;   // MOVT
    std::vector<Vec3>     normals;    // MONR
    std::vector<Vec2>     uvs;        // MOTV
    std::vector<uint16_t> indices;    // MOVI (triangle list)
    std::vector<uint8_t>  triMaterial;// MOPY material id per triangle
    std::vector<WmoBatch> batches;    // MOBA
    std::vector<WmoBspNode> bspNodes;     // MOBN collision BSP tree
    std::vector<uint16_t>   bspFaceIndices;// MOBV (indexes MOVI triangles)
    std::vector<Rgba>       vertexColors;  // MOCV (per-vertex BGRA, decoded)
    WmoLiquid               liquid;        // MLIQ (valid when liquid.present)
};

WmoGroup parseWmoGroup(const std::vector<uint8_t>& buf);

// The portal references for one group: root.portalRefs[group.moprIndex ..
// +group.moprCount), clamped to the array. Each names a portal (into MOPT) and
// the group it opens onto -- a group's interior-visibility adjacency list.
// Out-of-range ranges yield an empty list (defensive against odd data).
std::vector<WmoPortalRef> groupPortalRefs(const WmoRoot& root, const WmoGroup& group);

// A whole WMO: the root plus its parsed group geometry. Built by the asset
// loader (root file + the `_NNN.wmo` group files) and used for tight,
// per-triangle picking instead of the single root bounding box.
struct WmoModel {
    WmoRoot               root;
    std::vector<WmoGroup> groups;
};

// ---- collision raycast against a group's geometry ---------------------------
// Result of wmoRaycast: the nearest triangle the ray hits within tMax, its
// distance `t` along the (normalised) ray, and the triangle index into the
// group's MOVI list. `hit` is false when nothing is struck.
struct WmoRayHit {
    bool     hit      = false;
    float    t        = 0.0f;   // distance along `dir` (dir assumed unit length)
    uint32_t triangle = 0;      // MOVI triangle index of the hit face
};

// Cast a ray (origin + t*dir, t in [0, tMax]) against a WMO group's collision
// geometry, returning the nearest hit. When the group carries a MOBN/MOBV
// collision BSP the ray walks the tree (testing only faces in the leaves it can
// reach); otherwise it falls back to every triangle. `dir` should be unit
// length. This is the query the parsed-but-unused BSP existed for -- movement
// and line-of-sight against WMO interiors, the WMO analogue of terrain picking.
WmoRayHit wmoRaycast(const WmoGroup& group, const Vec3& origin, const Vec3& dir,
                     float tMax);

} // namespace wf
