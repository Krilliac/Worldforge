#include "wmo.hpp"
#include "byte_reader.hpp"
#include "chunk.hpp"

#include <cstring>
#include <stdexcept>

namespace wf {
namespace {

// Split a zero-terminated, 4-byte-aligned string blob (MOTX/MODN/MOGN), keeping
// the byte offset of each so it can be resolved exactly like the client does.
std::string strAt(const std::vector<char>& blob, uint32_t off) {
    if (off >= blob.size()) return std::string();
    const char* p = blob.data() + off;
    size_t maxLen = blob.size() - off;
    return std::string(p, ::strnlen(p, maxLen));
}

std::vector<std::string> splitBlob(const std::vector<char>& blob, uint32_t expected) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < blob.size()) {
        size_t start = i;
        while (i < blob.size() && blob[i] != '\0') ++i;
        if (i > start) out.push_back(std::string(blob.data() + start, i - start));
        ++i; // skip the NUL
        if (expected && out.size() >= expected) break;
    }
    return out;
}

} // namespace

WmoRoot parseWmoRoot(const std::vector<uint8_t>& buf, const ClientProfile& profile) {
    WmoRoot root;
    std::vector<char> motx, modn, mogn;

    forEachChunk(buf.data(), buf.size(), [&](const Chunk& c) {
        ByteReader r(c.data, c.size);
        if (c.magic == "MVER") {
            root.version = r.u32();
        } else if (c.magic == "MOHD") {
            root.nTextures    = r.u32();
            root.nGroups      = r.u32();
            root.nPortals     = r.u32();
            root.nLights      = r.u32();
            root.nDoodadNames = r.u32();
            root.nDoodadDefs  = r.u32();
            root.nDoodadSets  = r.u32();
            r.u32();                       // ambColor
            r.u32();                       // wmoID
            root.bboxMin = { r.f32(), r.f32(), r.f32() };
            root.bboxMax = { r.f32(), r.f32(), r.f32() };
            root.flags   = r.u16();
        } else if (c.magic == "MOTX") {
            motx.assign(c.data, c.data + c.size);
        } else if (c.magic == "MOMT") {
            while (r.remaining() >= 64) {
                WmoMaterial m;
                m.flags             = r.u32();
                m.shader            = r.u32();
                m.blendMode         = r.u32();
                m.diffuseNameOffset = r.u32();
                for (int i = 0; i < 12; ++i) r.u32();  // rest of the 64-byte record
                root.materials.push_back(m);
            }
        } else if (c.magic == "MOGN") {
            mogn.assign(c.data, c.data + c.size);
        } else if (c.magic == "MOGI") {
            while (r.remaining() >= 32) {
                WmoGroupInfo g;
                g.flags   = r.u32();
                g.bboxMin = { r.f32(), r.f32(), r.f32() };
                g.bboxMax = { r.f32(), r.f32(), r.f32() };
                g.nameOffset = r.i32();
                root.groups.push_back(g);
            }
        } else if (c.magic == "MODS") {
            while (r.remaining() >= 32) {
                WmoDoodadSet s;
                char name[21] = {0};
                for (int i = 0; i < 20; ++i) name[i] = static_cast<char>(r.u8());
                s.name = std::string(name, ::strnlen(name, 20));
                s.firstInstance = r.u32();
                s.numDoodads    = r.u32();
                r.u32();                   // unused
                root.doodadSets.push_back(s);
            }
        } else if (c.magic == "MOLT") {
            // 48-byte SMOLight records; count by chunkLen/48 (robust to MOHD).
            while (r.remaining() >= 48) {
                WmoLight L;
                L.type           = r.u8();
                L.useAttenuation = r.u8() != 0;
                r.u8(); r.u8();                              // padding
                uint8_t b = r.u8(), g = r.u8(), rr = r.u8(); r.u8();  // BGRA, drop A
                L.color      = { rr / 255.0f, g / 255.0f, b / 255.0f };
                L.position   = { r.f32(), r.f32(), r.f32() };
                L.intensity  = r.f32();
                L.attenStart = r.f32();
                L.attenEnd   = r.f32();
                for (int i = 0; i < 4; ++i) r.f32();         // trailing unknowns
                root.lights.push_back(L);
            }
        } else if (c.magic == "MODN") {
            modn.assign(c.data, c.data + c.size);
        } else if (c.magic == "MODD") {
            // Count is chunkLen/40, NOT nDoodadDefs (per wiki).
            while (r.remaining() >= 40) {
                WmoDoodad d;
                uint32_t packed = r.u32();
                d.nameOffset = packed & 0x00FFFFFF;   // low 24 bits; high 8 are flags
                d.position    = { r.f32(), r.f32(), r.f32() };
                d.orientation = { r.f32(), r.f32(), r.f32(), r.f32() };
                d.scale       = r.f32();
                r.u32();                   // color (BGRA)
                root.doodads.push_back(d);
            }
        } else if (c.magic == "MOSB") {
            // Single zero-terminated skybox model path (like MOTX/MODN entries).
            const char* p = reinterpret_cast<const char*>(c.data);
            root.skybox = std::string(p, ::strnlen(p, c.size));
        } else if (c.magic == "MOFG") {
            // 48-byte SMOFog records; count by chunkLen/48.
            while (r.remaining() >= 48) {
                WmoFog f;
                f.flags         = r.u32();
                f.position      = { r.f32(), r.f32(), r.f32() };
                f.smallerRadius = r.f32();
                f.largerRadius  = r.f32();
                f.end           = r.f32();        // first of two fog entries
                f.startScalar   = r.f32();
                uint8_t b = r.u8(), g = r.u8(), rr = r.u8(); r.u8();  // BGRA, drop A
                f.color = { rr / 255.0f, g / 255.0f, b / 255.0f };
                r.f32(); r.f32(); r.u32();         // skip the second fog entry
                root.fogs.push_back(f);
            }
        } else if (c.magic == "MOPV") {
            // C3Vector portal vertices.
            while (r.remaining() >= 12) root.portalVertices.push_back({ r.f32(), r.f32(), r.f32() });
        } else if (c.magic == "MOPT") {
            // 20-byte SMOPortal records.
            while (r.remaining() >= 20) {
                WmoPortal p;
                p.startVertex = r.u16();
                p.count       = r.u16();
                p.normal      = { r.f32(), r.f32(), r.f32() };
                p.planeDist   = r.f32();
                root.portals.push_back(p);
            }
        }
        return true;
    });

    // Resolve names.
    root.textures = splitBlob(motx, 0);
    for (WmoMaterial& m : root.materials) m.diffuseTexture = strAt(motx, m.diffuseNameOffset);
    for (WmoDoodad& d : root.doodads)     d.modelName      = strAt(modn, d.nameOffset);
    for (WmoGroupInfo& g : root.groups)
        if (g.nameOffset >= 0) g.name = strAt(mogn, static_cast<uint32_t>(g.nameOffset));

    // The client profile gates the WMO version: vanilla..Cata are v17. A newer
    // root (when an MVER is present and exceeds the profile) fails loud rather than
    // mis-parsing. version == 0 means no MVER chunk -> no gate (older test fixtures).
    if (root.version != 0 && root.version > profile.wmoVersion)
        throw std::runtime_error("WMO version newer than the client profile supports");

    return root;
}

WmoGroup parseWmoGroup(const std::vector<uint8_t>& buf) {
    WmoGroup grp;

    forEachChunk(buf.data(), buf.size(), [&](const Chunk& c) {
        if (c.magic != "MOGP") return true;  // group geometry lives inside MOGP

        // 68-byte MOGP header, then subchunks.
        if (c.size < 0x44) throw std::runtime_error("MOGP smaller than its 68-byte header");
        ByteReader h(c.data, 0x44);
        h.seek(0x08); grp.flags = h.u32();
        grp.bboxMin = { h.f32(), h.f32(), h.f32() };
        grp.bboxMax = { h.f32(), h.f32(), h.f32() };

        const uint8_t* sub = c.data + 0x44;
        size_t subLen = c.size - 0x44;
        forEachChunk(sub, subLen, [&](const Chunk& s) {
            ByteReader r(s.data, s.size);
            if (s.magic == "MOVT") {
                while (r.remaining() >= 12) grp.vertices.push_back({ r.f32(), r.f32(), r.f32() });
            } else if (s.magic == "MONR") {
                while (r.remaining() >= 12) grp.normals.push_back({ r.f32(), r.f32(), r.f32() });
            } else if (s.magic == "MOTV") {
                while (r.remaining() >= 8) { Vec2 uv; uv.x = r.f32(); uv.y = r.f32(); grp.uvs.push_back(uv); }
            } else if (s.magic == "MOVI") {
                while (r.remaining() >= 2) grp.indices.push_back(r.u16());
            } else if (s.magic == "MOPY") {
                while (r.remaining() >= 2) { r.u8(); grp.triMaterial.push_back(r.u8()); }
            } else if (s.magic == "MOBA") {
                while (r.remaining() >= 24) {
                    WmoBatch b;
                    for (int i = 0; i < 12; ++i) r.u8();   // bounding box (int16[6])
                    b.startIndex = r.u32();
                    b.indexCount = r.u16();
                    b.minIndex   = r.u16();
                    b.maxIndex   = r.u16();
                    b.flags      = r.u8();
                    b.materialId = r.u8();
                    grp.batches.push_back(b);
                }
            } else if (s.magic == "MOBN") {
                // 16-byte T_BSP_NODE collision tree records.
                while (r.remaining() >= 16) {
                    WmoBspNode n;
                    n.flags     = r.u16();
                    n.negChild  = static_cast<int16_t>(r.u16());
                    n.posChild  = static_cast<int16_t>(r.u16());
                    n.nFaces    = r.u16();
                    n.faceStart = r.u32();
                    n.planeDist = r.f32();
                    grp.bspNodes.push_back(n);
                }
            } else if (s.magic == "MOBV") {
                while (r.remaining() >= 2) grp.bspFaceIndices.push_back(r.u16());
            } else if (s.magic == "MOCV") {
                // CImVector BGRA per vertex; decode to Rgba.
                while (r.remaining() >= 4) {
                    uint8_t b = r.u8(), g = r.u8(), rr = r.u8(), a = r.u8();
                    grp.vertexColors.push_back({ rr, g, b, a });
                }
            }
            return true;
        });
        return true;
    });

    return grp;
}

} // namespace wf
