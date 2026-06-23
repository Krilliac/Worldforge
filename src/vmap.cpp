#include "vmap.hpp"
#include "byte_reader.hpp"

#include <cstring>
#include <set>
#include <stdexcept>

namespace wf {

namespace {
// Find the next occurrence of a 4-char tag at/after `from`. npos if absent.
size_t findTag(const std::vector<uint8_t>& b, const char* tag, size_t from) {
    for (size_t i = from; i + 4 <= b.size(); ++i)
        if (b[i] == (uint8_t)tag[0] && b[i+1] == (uint8_t)tag[1] &&
            b[i+2] == (uint8_t)tag[2] && b[i+3] == (uint8_t)tag[3])
            return i;
    return std::string::npos;
}
uint32_t rdU32(const std::vector<uint8_t>& b, size_t o) {
    return (uint32_t)b[o] | ((uint32_t)b[o+1]<<8) | ((uint32_t)b[o+2]<<16) | ((uint32_t)b[o+3]<<24);
}

// A validated "VERT"/"TRIM" chunk: tag at `pos`, then uint32 chunkSize, then
// uint32 count, then count elements of `elemBytes`. chunkSize must equal
// 4 + elemBytes*count and fit the buffer -- this rejects coincidental tag bytes
// inside float data.
bool validChunk(const std::vector<uint8_t>& b, size_t pos, uint32_t elemBytes,
                uint32_t& countOut, size_t& dataOff) {
    if (pos + 12 > b.size()) return false;
    uint32_t chunkSize = rdU32(b, pos + 4);
    uint32_t count     = rdU32(b, pos + 8);
    if (chunkSize != 4u + elemBytes * count) return false;
    if (pos + 8 + (size_t)elemBytes * count > b.size()) return false;
    countOut = count;
    dataOff  = pos + 12;          // first element (after tag, chunkSize, count)
    return true;
}
} // namespace

VmapModel parseWorldModel(const std::vector<uint8_t>& buf) {
    VmapModel model;
    if (buf.size() < 8 ||
        (std::memcmp(buf.data(), "VMAP_4.0", 8) != 0 &&
         std::memcmp(buf.data(), "VMAP_4.", 7) != 0))
        throw std::runtime_error("not a VMAP model (bad VMAP magic)");

    // Root WMO id from the WMOD chunk: "WMOD" + uint32 size + uint32 rootId.
    size_t wmod = findTag(buf, "WMOD", 8);
    if (wmod != std::string::npos && wmod + 12 <= buf.size())
        model.rootWmoId = rdU32(buf, wmod + 8);

    // Walk every VERT chunk; each marks a group. Group meta (AABox + flags + id
    // = 32 bytes) precedes the tag; the matching TRIM is the next TRIM after it.
    size_t scan = 0;
    while (true) {
        size_t vpos = findTag(buf, "VERT", scan);
        if (vpos == std::string::npos) break;
        uint32_t vcount = 0; size_t vdata = 0;
        if (!validChunk(buf, vpos, 12, vcount, vdata)) { scan = vpos + 4; continue; }

        VmapGroup g;
        if (vpos >= 32) {
            ByteReader m(buf.data() + (vpos - 32), 32);
            g.bmin  = { m.f32(), m.f32(), m.f32() };
            g.bmax  = { m.f32(), m.f32(), m.f32() };
            g.flags = m.u32();
            g.wmoId = m.u32();
        }
        g.vertices.reserve(vcount);
        { ByteReader vr(buf.data() + vdata, (size_t)vcount * 12);
          for (uint32_t i = 0; i < vcount; ++i) g.vertices.push_back({ vr.f32(), vr.f32(), vr.f32() }); }

        // matching TRIM (may be absent for a vertex-only group)
        size_t tpos = findTag(buf, "TRIM", vdata + (size_t)vcount * 12);
        uint32_t tcount = 0; size_t tdata = 0;
        if (tpos != std::string::npos && validChunk(buf, tpos, 12, tcount, tdata)) {
            g.triangles.reserve(tcount);
            ByteReader tr(buf.data() + tdata, (size_t)tcount * 12);
            for (uint32_t i = 0; i < tcount; ++i)
                g.triangles.push_back({ tr.u32(), tr.u32(), tr.u32() });
        }

        model.groups.push_back(std::move(g));
        scan = vdata + (size_t)vcount * 12;
    }
    return model;
}

Mesh vmapGroupToMesh(const VmapGroup& group) {
    Mesh mesh;
    mesh.vertices.reserve(group.vertices.size());
    for (const Vec3& v : group.vertices) mesh.vertices.push_back({ v, Vec3{0, 0, 1} });
    mesh.indices.reserve(group.triangles.size() * 3);
    for (const auto& t : group.triangles)
        for (uint32_t idx : t)
            if (idx < mesh.vertices.size()) mesh.indices.push_back(idx);
    mesh.indices.resize(mesh.indices.size() - (mesh.indices.size() % 3));
    return mesh;
}

void addCollision(DebugDraw& dd, const VmapModel& model, Rgba color, DebugCategory cat) {
    for (const VmapGroup& g : model.groups) {
        for (const auto& t : g.triangles) {
            if (t[0] >= g.vertices.size() || t[1] >= g.vertices.size() || t[2] >= g.vertices.size())
                continue;
            const Vec3& a = g.vertices[t[0]];
            const Vec3& b = g.vertices[t[1]];
            const Vec3& c = g.vertices[t[2]];
            dd.line(a, b, color, cat);
            dd.line(b, c, color, cat);
            dd.line(c, a, color, cat);
        }
    }
}

} // namespace wf
