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

struct WmoGroup {
    uint32_t flags = 0;
    Vec3     bboxMin, bboxMax;

    std::vector<Vec3>     vertices;   // MOVT
    std::vector<Vec3>     normals;    // MONR
    std::vector<Vec2>     uvs;        // MOTV
    std::vector<uint16_t> indices;    // MOVI (triangle list)
    std::vector<uint8_t>  triMaterial;// MOPY material id per triangle
    std::vector<WmoBatch> batches;    // MOBA
};

WmoGroup parseWmoGroup(const std::vector<uint8_t>& buf);

// A whole WMO: the root plus its parsed group geometry. Built by the asset
// loader (root file + the `_NNN.wmo` group files) and used for tight,
// per-triangle picking instead of the single root bounding box.
struct WmoModel {
    WmoRoot               root;
    std::vector<WmoGroup> groups;
};

} // namespace wf
