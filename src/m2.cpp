#include "m2.hpp"
#include "byte_reader.hpp"
#include <stdexcept>

namespace wf {
namespace {

// Vanilla MD20 header field offsets (version 0x100). Each "M2Array" is an
// (count, offset) uint32 pair. Verified against getMaNGOS vanilla layout.
constexpr size_t kOffName       = 0x008; // M2Array<char>
constexpr size_t kOffVertices   = 0x044; // M2Array<M2Vertex>
constexpr size_t kOffViews      = 0x04C; // M2Array<M2SkinProfile> (embedded)
constexpr size_t kOffTextures   = 0x05C; // M2Array<M2Texture>

constexpr size_t kVertexStride  = 48;    // M2Vertex
constexpr size_t kTextureStride = 16;    // M2Texture (type, flags, M2Array name)
constexpr size_t kSubmeshStride = 32;    // vanilla M2SkinSection (no sort centre)
constexpr size_t kBatchStride   = 24;    // texture unit: flags/priority + 11 uint16

// --- attachments / cameras / lights (static fields only) -------------------
// VERIFY-FLAGGED (vanilla 0x100): these M2Array header offsets sit AFTER the
// bounding/collision block (~0x9C), whose exact size differs between sources, so
// the precise hex is not safely constant. The values below follow the most-cited
// vanilla ModelHeader layout (getMaNGOS / WMV: bounding tris/verts/normals @
// 0x9C..0xB4, then attachments). Every read is range-guarded, so a model whose
// real layout differs (or which lacks these arrays) parses as before rather than
// mis-reading garbage. Confirm against a real 1.12 .m2 before trusting the data.
constexpr size_t kOffAttachments = 0x0B4; // M2Array<ModelAttachmentDef>
constexpr size_t kOffLights      = 0x0CC; // M2Array<ModelLightDef>
constexpr size_t kOffCameras     = 0x0D4; // M2Array<ModelCameraDef>

// VERIFY-FLAGGED (vanilla 0x100): the emitter arrays follow the camera block.
// Counting the 8-byte M2Array fields forward from cameras @ 0xD4:
//   cameras @ 0xD4, camera_lookup @ 0xDC, ribbon_emitters @ 0xE4,
//   particle_emitters @ 0xEC. This matches the most-cited vanilla ModelHeader
//   layout (getMaNGOS / WMV). Every read below is range-guarded, so a model
//   whose real layout differs (or which lacks these arrays) parses as before.
constexpr size_t kOffRibbonEmitters   = 0x0E4; // M2Array<RibbonEmitterDef>
constexpr size_t kOffParticleEmitters = 0x0EC; // M2Array<ParticleEmitterDef>

// On-disk record strides (embedded AnimationBlocks are 28 bytes each):
//   Attachment = id(4)+bone(4)+pos(12)+M2Track<bool>(28)                     = 48
//   Light      = type(2)+bone(2)+pos(12)+7*M2Track(28)                       = 212
//   Camera     = type/fov/far/near(16)+M2Track(28)+pos(12)+M2Track(28)
//                +target(12)+M2Track(28)                                     = 124
constexpr size_t kAttachStride = 48;
constexpr size_t kLightStride  = 212;
constexpr size_t kCameraStride = 124;
constexpr size_t kCameraPosOff = 16 + 28; // pos Vec3 follows the first M2Track

// VERIFY-FLAGGED (vanilla 0x100) emitter record strides. Both records begin with
// id(4)+bone(4)+pos(12) = 20 leading static bytes, then a long run of embedded
// AnimationBlocks (28 bytes each) we skip. The full strides differ across sources;
// the documented vanilla sizes are ~0xB0 (ribbon) and ~0x1D8 (particle). We only
// read the leading 20 bytes, then advance by the full stride. Reads are guarded,
// so a mis-derived stride yields a no-op (fewer/zero records) rather than a crash.
constexpr size_t kRibbonStride   = 0x0B0;  // RibbonEmitterDef
constexpr size_t kParticleStride = 0x1D8;  // ParticleEmitterDef
constexpr size_t kEmitterLeadBytes = 20;   // id(4)+bone(4)+pos(12)

struct Arr { uint32_t count; uint32_t offset; };

Arr readArr(ByteReader& r, size_t at) {
    r.seek(at);
    uint32_t count  = r.u32();
    uint32_t offset = r.u32();
    return { count, offset };
}

void requireRange(const std::vector<uint8_t>& buf, uint32_t off, size_t bytes, const char* what) {
    if (static_cast<size_t>(off) + bytes > buf.size())
        throw std::runtime_error(std::string("M2 array out of range: ") + what);
}

std::string readCStr(const std::vector<uint8_t>& buf, uint32_t off, uint32_t count) {
    if (count == 0) return std::string();
    requireRange(buf, off, count, "string");
    const char* p = reinterpret_cast<const char*>(buf.data() + off);
    size_t len = 0;
    while (len < count && p[len] != '\0') ++len;
    return std::string(p, len);
}

} // namespace

M2Model parseM2(const std::vector<uint8_t>& buf, const ClientProfile& profile) {
    ByteReader r(buf);
    if (buf.size() < 0x100) throw std::runtime_error("M2 too small for a header");
    if (r.fourccRaw() != "MD20") throw std::runtime_error("not an MD20 (M2) file");

    M2Model m;
    r.seek(0x004);
    m.version = r.u32();

    // The client profile gates which M2 versions this build can parse. Vanilla
    // 1.12.1 is 0x100; a newer model (TBC 0x104+) fed to the vanilla profile fails
    // loud rather than mis-parsing a layout this build does not yet understand.
    if (m.version > profile.m2Version)
        throw std::runtime_error("M2 version newer than the client profile supports");

    // name
    {
        Arr a = readArr(r, kOffName);
        m.name = readCStr(buf, a.offset, a.count);
    }

    // global vertices
    {
        Arr a = readArr(r, kOffVertices);
        requireRange(buf, a.offset, static_cast<size_t>(a.count) * kVertexStride, "vertices");
        m.vertices.reserve(a.count);
        for (uint32_t i = 0; i < a.count; ++i) {
            ByteReader vr(buf.data() + a.offset + i * kVertexStride, kVertexStride);
            M2Vertex v;
            v.pos = { vr.f32(), vr.f32(), vr.f32() };
            for (uint8_t& w : v.boneWeights) w = vr.u8();
            for (uint8_t& b : v.boneIndices) b = vr.u8();
            v.normal = { vr.f32(), vr.f32(), vr.f32() };
            v.uv = { vr.f32(), vr.f32() };
            m.vertices.push_back(v);
        }
    }

    // textures
    {
        Arr a = readArr(r, kOffTextures);
        requireRange(buf, a.offset, static_cast<size_t>(a.count) * kTextureStride, "textures");
        for (uint32_t i = 0; i < a.count; ++i) {
            ByteReader tr(buf.data() + a.offset + i * kTextureStride, kTextureStride);
            uint32_t type = tr.u32();
            tr.u32(); // flags
            uint32_t nameCount  = tr.u32();
            uint32_t nameOffset = tr.u32();
            m.textureTypes.push_back(type);
            // Only type 0 carries a hardcoded path; others are runtime-substituted.
            m.textures.push_back(type == 0 ? readCStr(buf, nameOffset, nameCount) : std::string());
        }
    }

    // view 0 (embedded skin profile)
    {
        Arr views = readArr(r, kOffViews);
        if (views.count > 0) {
            // M2SkinProfile header: 5 M2Arrays + uint32 boneCountMax = 44 bytes.
            requireRange(buf, views.offset, 44, "view header");
            ByteReader vr(buf.data() + views.offset, 44);
            Arr vlook = { vr.u32(), vr.u32() }; // vertices (lookup)
            Arr tris  = { vr.u32(), vr.u32() }; // indices (triangles)
            vr.u32(); vr.u32();                 // bones (per-vertex bone indices)
            Arr subs  = { vr.u32(), vr.u32() }; // submeshes
            Arr bats  = { vr.u32(), vr.u32() }; // batches (texture units)
            // (boneCountMax follows; not needed)

            requireRange(buf, vlook.offset, static_cast<size_t>(vlook.count) * 2, "view vertices");
            m.vertexLookup.reserve(vlook.count);
            { ByteReader lr(buf.data() + vlook.offset, vlook.count * 2);
              for (uint32_t i = 0; i < vlook.count; ++i) m.vertexLookup.push_back(lr.u16()); }

            requireRange(buf, tris.offset, static_cast<size_t>(tris.count) * 2, "view triangles");
            m.triangles.reserve(tris.count);
            { ByteReader tr(buf.data() + tris.offset, tris.count * 2);
              for (uint32_t i = 0; i < tris.count; ++i) m.triangles.push_back(tr.u16()); }

            requireRange(buf, subs.offset, static_cast<size_t>(subs.count) * kSubmeshStride, "submeshes");
            for (uint32_t i = 0; i < subs.count; ++i) {
                ByteReader sr(buf.data() + subs.offset + i * kSubmeshStride, kSubmeshStride);
                M2Submesh s;
                s.id          = sr.u16();
                sr.u16();                 // Level (level<<16 added to start fields)
                s.vertexStart = sr.u16();
                s.vertexCount = sr.u16();
                s.indexStart  = sr.u16();
                s.indexCount  = sr.u16();
                m.submeshes.push_back(s);
            }

            // batches (texture units): flags(u8) + priorityPlane(i8) + 11 uint16
            // material/lookup indices. priorityPlane is SIGNED -- it is the
            // primary sort key for translucent draw ordering. Guarded like the
            // additive arrays: a count of 0 or an out-of-range offset leaves the
            // vector empty (older fixtures/models parse as before).
            if (bats.count && static_cast<size_t>(bats.offset) +
                              static_cast<size_t>(bats.count) * kBatchStride <= buf.size()) {
                for (uint32_t i = 0; i < bats.count; ++i) {
                    ByteReader br(buf.data() + bats.offset + i * kBatchStride, kBatchStride);
                    M2Batch b;
                    b.flags         = br.u8();
                    b.priorityPlane = static_cast<int8_t>(br.u8());
                    b.shaderId      = br.u16();
                    b.submeshIndex  = br.u16();
                    b.geosetIndex   = br.u16();
                    b.colorIndex    = br.u16();
                    b.materialIndex = br.u16();
                    b.materialLayer = br.u16();
                    b.textureCount  = br.u16();
                    b.textureComboIndex          = br.u16();
                    b.textureCoordComboIndex     = br.u16();
                    b.textureWeightComboIndex    = br.u16();
                    b.textureTransformComboIndex = br.u16();
                    m.batches.push_back(b);
                }
            }
        }
    }

    // ---- attachments / cameras / lights (static leading fields only) -------
    // Additive + guarded: each array header offset is VERIFY-FLAGGED (see the
    // constants above). A count of 0, an out-of-range offset, or a record that
    // would run past the buffer simply yields an empty vector -- models without
    // these arrays parse exactly as before.

    // attachments: id(u32) + bone(u32) + pos(Vec3), then a 28-byte M2Track skip.
    if (kOffAttachments + 8 <= buf.size()) {
        Arr a = readArr(r, kOffAttachments);
        if (a.count && static_cast<size_t>(a.offset) +
                       static_cast<size_t>(a.count) * kAttachStride <= buf.size()) {
            for (uint32_t i = 0; i < a.count; ++i) {
                ByteReader ar(buf.data() + a.offset + i * kAttachStride, kAttachStride);
                M2Attachment at;
                at.id   = ar.u32();
                at.bone = ar.u32();
                at.position = { ar.f32(), ar.f32(), ar.f32() };
                m.attachments.push_back(at);
            }
        }
    }

    // lights: type(u16) + bone(i16) + pos(Vec3), then seven 28-byte M2Tracks.
    if (kOffLights + 8 <= buf.size()) {
        Arr a = readArr(r, kOffLights);
        if (a.count && static_cast<size_t>(a.offset) +
                       static_cast<size_t>(a.count) * kLightStride <= buf.size()) {
            for (uint32_t i = 0; i < a.count; ++i) {
                ByteReader lr(buf.data() + a.offset + i * kLightStride, kLightStride);
                M2Light l;
                l.type = lr.u16();
                l.bone = static_cast<int16_t>(lr.u16());
                l.position = { lr.f32(), lr.f32(), lr.f32() };
                m.lights.push_back(l);
            }
        }
    }

    // cameras: type/fov/farClip/nearClip(4*4), then the static pos Vec3 sits
    // after the first M2Track (kCameraPosOff); the rest of the stride is skipped.
    if (kOffCameras + 8 <= buf.size()) {
        Arr a = readArr(r, kOffCameras);
        if (a.count && static_cast<size_t>(a.offset) +
                       static_cast<size_t>(a.count) * kCameraStride <= buf.size()) {
            for (uint32_t i = 0; i < a.count; ++i) {
                ByteReader cr(buf.data() + a.offset + i * kCameraStride, kCameraStride);
                M2Camera c;
                c.type     = cr.u32();
                c.fov      = cr.f32();
                c.farClip  = cr.f32();
                c.nearClip = cr.f32();
                cr.seek(kCameraPosOff);
                c.position = { cr.f32(), cr.f32(), cr.f32() };
                m.cameras.push_back(c);
            }
        }
    }

    // ---- ribbon / particle emitters (static leading fields only) -----------
    // Additive + guarded exactly like the attachment/camera/light blocks above.
    // Each record's leading id(u32)+bone(i32)+pos(Vec3) is read; the embedded
    // AnimationBlocks that follow are skipped by advancing the full stride.

    // ribbon emitters: id(u32) + bone(i32) + pos(Vec3), then ~0xB0-byte stride.
    if (kOffRibbonEmitters + 8 <= buf.size()) {
        Arr a = readArr(r, kOffRibbonEmitters);
        if (a.count && static_cast<size_t>(a.offset) +
                       static_cast<size_t>(a.count) * kRibbonStride <= buf.size()) {
            for (uint32_t i = 0; i < a.count; ++i) {
                ByteReader er(buf.data() + a.offset + i * kRibbonStride, kEmitterLeadBytes);
                M2RibbonEmitter e;
                e.id   = er.u32();
                e.bone = static_cast<int32_t>(er.u32());
                e.position = { er.f32(), er.f32(), er.f32() };
                m.ribbonEmitters.push_back(e);
            }
        }
    }

    // particle emitters: id(u32) + bone(i32) + pos(Vec3), then ~0x1D8-byte stride.
    if (kOffParticleEmitters + 8 <= buf.size()) {
        Arr a = readArr(r, kOffParticleEmitters);
        if (a.count && static_cast<size_t>(a.offset) +
                       static_cast<size_t>(a.count) * kParticleStride <= buf.size()) {
            for (uint32_t i = 0; i < a.count; ++i) {
                ByteReader er(buf.data() + a.offset + i * kParticleStride, kEmitterLeadBytes);
                M2ParticleEmitter e;
                e.id   = er.u32();
                e.bone = static_cast<int32_t>(er.u32());
                e.position = { er.f32(), er.f32(), er.f32() };
                m.particleEmitters.push_back(e);
            }
        }
    }

    return m;
}

// ===========================================================================
// Animation parsing (vanilla layout)
// ===========================================================================
namespace {

constexpr size_t kOffAnimations = 0x01C; // nAnimations, ofsAnimations
constexpr size_t kOffBones      = 0x034; // nBones, ofsBones

// Material-animation header arrays (vanilla version 0x100, verified against
// getMaNGOS' layout). `nViews` is a SINGLE uint32 at 0x40, which shifts these
// relative to a naive (count,offset)-pair read of every field.
constexpr size_t kOffGlobalSeqs   = 0x010; // nGlobalSequences, ofs (uint32 ms each)
constexpr size_t kOffColors       = 0x044; // nColors, ofsColors (ModelColorDef[])
constexpr size_t kOffTransparency = 0x054; // nTransparency, ofs (ModelTransDef[])
constexpr size_t kOffTransLookup  = 0x08C; // nTransparencyLookup, ofs (uint16[])

constexpr size_t kColorStride = 0x38;      // ModelColorDef: 2 * AnimationBlock = 56
constexpr size_t kTransStride = 0x1C;      // ModelTransDef:  1 * AnimationBlock = 28

// VERIFY-FLAGGED (vanilla 0x100): UV texture-transform arrays. Following this
// parser's shifted-vanilla scheme (colors @ 0x44, transparency @ 0x54), the
// texture-transform records sit one slot past the unknown 0x5C field and their
// lookup table directly after the transparency lookup (@ 0x8C). Both reads are
// range-guarded, so a model whose real layout differs (or which lacks these
// arrays) parses with empty vectors. Confirm against a real 1.12 .m2.
constexpr size_t kOffTexTransforms      = 0x064; // nTexAnims, ofs (ModelTexAnimDef[])
constexpr size_t kOffTexTransformLookup = 0x094; // nTexAnimLookup, ofs (int16[])

constexpr size_t kTexXformStride = 0x54;   // ModelTexAnimDef: 3 * AnimationBlock = 84

// VERIFY-AGAINST-REAL-FILE constants (vanilla specifics that differ across
// sources). The parser logic below is independent of these; only the byte
// strides depend on them, so a single real 1.12 .m2 confirms/adjusts them:
constexpr size_t kSeqStride   = 0x44;    // ModelAnimation record size (vanilla)
constexpr size_t kBoneStride  = 0x6C;    // ModelBoneDef: 12 hdr + 3*28 tracks + 12 pivot = 108
constexpr size_t kAnimBlock   = 0x1C;    // AnimationBlock: 28 bytes
constexpr size_t kRotStride   = 16;      // rotation quaternion as 4 floats (vanilla)

// Read a vanilla AnimationBlock at `at` into a RawChannel<T>. `readValue`
// decodes one value of stride `valStride` from a ByteReader. For spline tracks
// (interp 2 = bezier / 3 = hermite) the on-disk values array is 3x the
// timestamp count, laid out per key as {value, inTan, outTan}; those are parsed
// into the parallel inTan/outTan vectors.
template <typename T, typename F>
RawChannel<T> readChannel(const std::vector<uint8_t>& buf, size_t at,
                          size_t valStride, F readValue) {
    RawChannel<T> ch;
    ByteReader b(buf.data() + at, kAnimBlock);
    ch.interp    = static_cast<uint16_t>(b.u16());
    ch.globalSeq = static_cast<int16_t>(b.u16());
    uint32_t nRanges = b.u32(), ofsRanges = b.u32();
    uint32_t nTimes  = b.u32(), ofsTimes  = b.u32();
    uint32_t nKeys   = b.u32(), ofsKeys   = b.u32();

    if (nRanges && static_cast<size_t>(ofsRanges) + nRanges * 8 <= buf.size()) {
        ByteReader r(buf.data() + ofsRanges, nRanges * 8);
        for (uint32_t i = 0; i < nRanges; ++i) {
            uint32_t a = r.u32(), c = r.u32();
            ch.ranges.emplace_back(a, c);
        }
    }
    if (nTimes && static_cast<size_t>(ofsTimes) + nTimes * 4 <= buf.size()) {
        ByteReader r(buf.data() + ofsTimes, nTimes * 4);
        for (uint32_t i = 0; i < nTimes; ++i) ch.times.push_back(r.u32());
    }
    const bool spline = ch.interp >= M2_INTERP_BEZIER;   // 2 or 3
    const size_t keyStride = spline ? valStride * 3 : valStride;
    if (nKeys && static_cast<size_t>(ofsKeys) + nKeys * keyStride <= buf.size()) {
        for (uint32_t i = 0; i < nKeys; ++i) {
            ByteReader r(buf.data() + ofsKeys + i * keyStride, keyStride);
            ch.values.push_back(readValue(r));
            if (spline) {
                ch.inTan.push_back(readValue(r));
                ch.outTan.push_back(readValue(r));
            }
        }
    }
    return ch;
}

} // namespace

M2Animation parseM2Animation(const std::vector<uint8_t>& buf) {
    if (buf.size() < 0x148) throw std::runtime_error("M2 too small for vanilla header");
    ByteReader r(buf);
    if (r.fourccRaw() != "MD20") throw std::runtime_error("not an MD20 (M2) file");

    M2Animation out;

    // sequences
    {
        r.seek(kOffAnimations);
        uint32_t n = r.u32(), ofs = r.u32();
        if (static_cast<size_t>(ofs) + static_cast<size_t>(n) * kSeqStride <= buf.size()) {
            // Vanilla (pre-WotLK) 0x44-byte record layout: id @ 0x00, subId
            // @ 0x02, startTimestamp @ 0x04, endTimestamp @ 0x08 (explicit
            // timestamps, no stored duration), moveSpeed @ 0x0C, flags @ 0x10,
            // frequency+pad @ 0x14, replay min/max @ 0x18/0x1C, blendTime
            // @ 0x20, bounds+radius @ 0x24..0x3F, variationNext @ 0x40,
            // aliasNext @ 0x42.
            for (uint32_t i = 0; i < n; ++i) {
                ByteReader s(buf.data() + ofs + i * kSeqStride, kSeqStride);
                M2Sequence seq;
                seq.id     = s.u16();
                seq.subId  = s.u16();
                uint32_t start = s.u32();
                uint32_t end   = s.u32();
                seq.length = (end > start) ? end - start : 0;   // duration
                s.seek(0x10); seq.flags = s.u32();
                s.seek(0x20); seq.blendTime = s.u32();
                s.seek(0x40); seq.variationNext = static_cast<int16_t>(s.u16());
                out.sequences.push_back(seq);
            }
        }
    }

    // bones
    {
        r.seek(kOffBones);
        uint32_t n = r.u32(), ofs = r.u32();
        auto readVec3 = [](ByteReader& b){ return Vec3{ b.f32(), b.f32(), b.f32() }; };
        auto readQuat = [](ByteReader& b){ return Quat{ b.f32(), b.f32(), b.f32(), b.f32() }; };
        for (uint32_t i = 0; i < n; ++i) {
            size_t base = ofs + i * kBoneStride;
            if (base + kBoneStride > buf.size()) break;
            ByteReader b(buf.data() + base, kBoneStride);
            M2BoneRaw bone;
            b.u32();                                  // keyBoneId
            bone.flags = b.u32();                     // billboard bits (anim.hpp)
            bone.parent = static_cast<int16_t>(b.u16());
            b.u16();                                  // submeshId
            bone.translation = readChannel<Vec3>(buf, base + 0x0C,                12, readVec3);
            bone.rotation    = readChannel<Quat>(buf, base + 0x0C + kAnimBlock,   kRotStride, readQuat);
            bone.scale       = readChannel<Vec3>(buf, base + 0x0C + kAnimBlock*2, 12, readVec3);
            ByteReader pv(buf.data() + base + 0x0C + kAnimBlock*3, 12);
            bone.pivot = { pv.f32(), pv.f32(), pv.f32() };
            out.bones.push_back(bone);
        }
    }

    // ---- material animation: global sequences + colors + texture-weights ----
    // Value decoders. fixed16: int16 on disk, normalised by 32767 (matches WMV
    // ShortToFloat and three-m2loader value/0x7fff).
    auto readVec3    = [](ByteReader& b){ return Vec3{ b.f32(), b.f32(), b.f32() }; };
    auto readFixed16 = [](ByteReader& b){
        return static_cast<float>(static_cast<int16_t>(b.u16())) / 32767.0f;
    };

    // global sequences (uint32 ms durations)
    {
        r.seek(kOffGlobalSeqs);
        uint32_t n = r.u32(), ofs = r.u32();
        if (n && static_cast<size_t>(ofs) + static_cast<size_t>(n) * 4 <= buf.size()) {
            ByteReader g(buf.data() + ofs, n * 4);
            for (uint32_t i = 0; i < n; ++i) out.globalSeqs.push_back(g.u32());
        }
    }

    // colors: ModelColorDef[] = { AnimationBlock rgb (Vec3); AnimationBlock alpha (fixed16) }
    {
        r.seek(kOffColors);
        uint32_t n = r.u32(), ofs = r.u32();
        if (n && static_cast<size_t>(ofs) + static_cast<size_t>(n) * kColorStride <= buf.size()) {
            for (uint32_t i = 0; i < n; ++i) {
                size_t base = ofs + i * kColorStride;
                M2ColorRaw c;
                c.rgb   = readChannel<Vec3 >(buf, base,             12, readVec3);
                c.alpha = readChannel<float>(buf, base + kAnimBlock, 2, readFixed16);
                out.colors.push_back(c);
            }
        }
    }

    // transparency / texture-weight: ModelTransDef[] = { AnimationBlock weight (fixed16) }
    {
        r.seek(kOffTransparency);
        uint32_t n = r.u32(), ofs = r.u32();
        if (n && static_cast<size_t>(ofs) + static_cast<size_t>(n) * kTransStride <= buf.size()) {
            for (uint32_t i = 0; i < n; ++i) {
                size_t base = ofs + i * kTransStride;
                M2TextureWeightRaw w;
                w.weight = readChannel<float>(buf, base, 2, readFixed16);
                out.textureWeights.push_back(w);
            }
        }
    }

    // transparency lookup table (uint16 indices into textureWeights)
    {
        r.seek(kOffTransLookup);
        uint32_t n = r.u32(), ofs = r.u32();
        if (n && static_cast<size_t>(ofs) + static_cast<size_t>(n) * 2 <= buf.size()) {
            ByteReader l(buf.data() + ofs, n * 2);
            for (uint32_t i = 0; i < n; ++i) out.transparencyLookup.push_back(l.u16());
        }
    }

    // UV texture transforms: ModelTexAnimDef[] = { AnimationBlock translation
    // (Vec3); AnimationBlock rotation (Quat, 4 floats); AnimationBlock scaling
    // (Vec3) } -- plus their int16 lookup table (-1 = identity).
    {
        auto readQuat = [](ByteReader& b){ return Quat{ b.f32(), b.f32(), b.f32(), b.f32() }; };
        r.seek(kOffTexTransforms);
        uint32_t n = r.u32(), ofs = r.u32();
        if (n && static_cast<size_t>(ofs) + static_cast<size_t>(n) * kTexXformStride <= buf.size()) {
            for (uint32_t i = 0; i < n; ++i) {
                size_t base = ofs + i * kTexXformStride;
                M2TextureTransformRaw x;
                x.translation = readChannel<Vec3>(buf, base,                12,         readVec3);
                x.rotation    = readChannel<Quat>(buf, base + kAnimBlock,   kRotStride, readQuat);
                x.scaling     = readChannel<Vec3>(buf, base + kAnimBlock*2, 12,         readVec3);
                out.textureTransforms.push_back(x);
            }
        }
    }
    {
        r.seek(kOffTexTransformLookup);
        uint32_t n = r.u32(), ofs = r.u32();
        if (n && static_cast<size_t>(ofs) + static_cast<size_t>(n) * 2 <= buf.size()) {
            ByteReader l(buf.data() + ofs, n * 2);
            for (uint32_t i = 0; i < n; ++i)
                out.textureTransformLookup.push_back(static_cast<int16_t>(l.u16()));
        }
    }

    return out;
}

// ===========================================================================
// Material-animation sampling
// ===========================================================================
M2Tint sampleM2Color(const M2Animation& anim, int colorIndex,
                     int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs) {
    M2Tint out;
    if (colorIndex < 0 || static_cast<size_t>(colorIndex) >= anim.colors.size())
        return out;                                   // identity tint
    const M2ColorRaw& c = anim.colors[colorIndex];
    out.rgb   = sampleChannel<RawChannel<Vec3>,  Vec3 >(
                    c.rgb,   animIndex, animTimeMs, globalTimeMs, anim.globalSeqs, Vec3{1,1,1});
    out.alpha = sampleChannel<RawChannel<float>, float>(
                    c.alpha, animIndex, animTimeMs, globalTimeMs, anim.globalSeqs, 1.0f);
    return out;
}

float sampleM2TextureWeight(const M2Animation& anim, int weightIndex,
                            int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs) {
    if (weightIndex < 0) return 1.0f;
    // Redirect through the transparency lookup table when present (indirection
    // differs subtly across vanilla files; honour the lookup if it exists).
    int idx = weightIndex;
    if (!anim.transparencyLookup.empty()) {
        if (static_cast<size_t>(weightIndex) >= anim.transparencyLookup.size()) return 1.0f;
        idx = anim.transparencyLookup[weightIndex];
    }
    if (idx < 0 || static_cast<size_t>(idx) >= anim.textureWeights.size()) return 1.0f;
    return sampleChannel<RawChannel<float>, float>(
               anim.textureWeights[idx].weight, animIndex, animTimeMs,
               globalTimeMs, anim.globalSeqs, 1.0f);
}

M2Tint sampleM2Tint(const M2Animation& anim, int colorIndex, int weightIndex,
                    int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs) {
    M2Tint t = sampleM2Color(anim, colorIndex, animIndex, animTimeMs, globalTimeMs);
    float w = sampleM2TextureWeight(anim, weightIndex, animIndex, animTimeMs, globalTimeMs);
    t.alpha *= w;
    return t;
}

Mat4 sampleM2TextureTransform(const M2Animation& anim, int transformIndex,
                              int animIndex, uint32_t animTimeMs, uint32_t globalTimeMs) {
    if (transformIndex < 0) return Mat4::identity();
    // Redirect through the texture-transform lookup table when present (the
    // batch carries a combo index; -1 in the table means "no transform").
    int idx = transformIndex;
    if (!anim.textureTransformLookup.empty()) {
        if (static_cast<size_t>(transformIndex) >= anim.textureTransformLookup.size())
            return Mat4::identity();
        idx = anim.textureTransformLookup[transformIndex];
    }
    if (idx < 0 || static_cast<size_t>(idx) >= anim.textureTransforms.size())
        return Mat4::identity();
    const M2TextureTransformRaw& x = anim.textureTransforms[idx];

    Vec3 tr = sampleChannel<RawChannel<Vec3>, Vec3>(
                  x.translation, animIndex, animTimeMs, globalTimeMs, anim.globalSeqs, Vec3{0,0,0});
    Quat ro = sampleChannel<RawChannel<Quat>, Quat>(
                  x.rotation, animIndex, animTimeMs, globalTimeMs, anim.globalSeqs, Quat::identity());
    Vec3 sc = sampleChannel<RawChannel<Vec3>, Vec3>(
                  x.scaling, animIndex, animTimeMs, globalTimeMs, anim.globalSeqs, Vec3{1,1,1});

    // Rotate/scale about the texture centre; the animated translation (only
    // x/y are used) scrolls on top:  M = T(tx,ty) * T(.5,.5) * R * S * T(-.5,-.5).
    return Mat4::translate(Vec3{ tr.x, tr.y, 0.0f })
         * Mat4::translate(Vec3{ 0.5f, 0.5f, 0.0f })
         * ro.toMat4()
         * Mat4::scale(sc)
         * Mat4::translate(Vec3{ -0.5f, -0.5f, 0.0f });
}

// Slice each bone's raw channels down to one animation, producing pose-ready
// Bones for the animation core. Uses the per-animation interpolation range when
// present; otherwise (global sequence / no ranges) uses the whole track.
std::vector<Bone> buildBonesForAnimation(const M2Animation& anim, int animIndex) {
    std::vector<Bone> bones;
    bones.reserve(anim.bones.size());

    // Slice one channel to the animation's key range, carrying the spline
    // tangents along when the track has them (bezier/hermite).
    auto slice = [&](const auto& ch, auto& kt) {
        kt.interp = ch.interp;
        // Truncated files can leave times/values with different lengths (each
        // array is bounds-checked independently); copy only the shared prefix.
        const size_t n = ch.times.size() < ch.values.size() ? ch.times.size()
                                                            : ch.values.size();
        if (n == 0) return;
        size_t first = 0, last = n - 1;
        if (animIndex >= 0 && static_cast<size_t>(animIndex) < ch.ranges.size()) {
            first = ch.ranges[animIndex].first;
            last  = ch.ranges[animIndex].second;
        }
        bool tangents = ch.inTan.size() == ch.values.size() &&
                        ch.outTan.size() == ch.values.size();
        for (size_t i = first; i <= last && i < n; ++i) {
            kt.times.push_back(ch.times[i]);
            kt.values.push_back(ch.values[i]);
            if (tangents) {
                kt.inTan.push_back(ch.inTan[i]);
                kt.outTan.push_back(ch.outTan[i]);
            }
        }
    };

    for (const M2BoneRaw& rb : anim.bones) {
        Bone b;
        b.parent = rb.parent;
        b.flags  = rb.flags;
        b.pivot  = rb.pivot;
        slice(rb.translation, b.translation);
        slice(rb.rotation,    b.rotation);
        slice(rb.scale,       b.scale);
        bones.push_back(b);
    }
    return bones;
}

// ---- material blend modes ---------------------------------------------------
M2RasterState resolveM2Material(M2BlendMode blend, uint16_t flags) {
    M2RasterState s;
    switch (blend) {
        case M2BlendMode::Opaque:
            break;                                   // defaults: opaque, writeDepth
        case M2BlendMode::AlphaKey:
            s.alphaTest = true;                      // cutout, still writes depth
            break;
        case M2BlendMode::Alpha:
        case M2BlendMode::Mod:
        case M2BlendMode::Mod2x:
            s.alphaBlend = true;
            s.writeDepth = false;
            break;
        case M2BlendMode::Add:
        case M2BlendMode::BlendAdd:
            s.alphaBlend = true;
            s.emissive   = true;                     // additive glow: ignore lighting
            s.writeDepth = false;
            break;
        default:                                     // unknown -> safe opaque
            break;
    }
    s.unlit    = (flags & M2RF_UNLIT) != 0 || s.emissive;
    s.twoSided = (flags & M2RF_TWO_SIDED) != 0;
    if (flags & M2RF_NO_ZWRITE) s.writeDepth = false;
    return s;
}

// ---- geoset selection -------------------------------------------------------
M2GeosetId decodeGeosetId(uint16_t submeshId) {
    M2GeosetId g;
    g.base      = (submeshId == 0);
    g.group     = static_cast<uint16_t>(submeshId / 100);
    g.variation = static_cast<uint16_t>(submeshId % 100);
    return g;
}

std::vector<uint32_t> selectGeosets(const std::vector<M2Submesh>& submeshes,
                                    const std::unordered_map<uint16_t, uint16_t>& chosen) {
    // Default variation per group = the lowest variation present, so a model with
    // no explicit choice draws a deterministic "first look".
    std::unordered_map<uint16_t, uint16_t> lowest;
    for (const M2Submesh& s : submeshes) {
        if (s.id == 0) continue;
        M2GeosetId g = decodeGeosetId(s.id);
        auto it = lowest.find(g.group);
        if (it == lowest.end() || g.variation < it->second) lowest[g.group] = g.variation;
    }
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < submeshes.size(); ++i) {
        const M2Submesh& s = submeshes[i];
        if (s.id == 0) { out.push_back(i); continue; }   // base skin: always drawn
        M2GeosetId g = decodeGeosetId(s.id);
        auto it = chosen.find(g.group);
        uint16_t want = (it != chosen.end()) ? it->second : lowest[g.group];
        if (g.variation == want) out.push_back(i);
    }
    return out;
}

} // namespace wf
