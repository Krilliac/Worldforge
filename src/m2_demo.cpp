// ---------------------------------------------------------------------------
// wforge-m2dump : M2 model geometry validation on REAL vanilla 1.12.1 models.
//
//   Opens a client's MPQ chain, reads a real M2 (applying the engine's
//   .mdx/.mdl -> .m2 extension swap), parses it (parseM2 + parseM2Animation),
//   skins the static bind pose (skinM2), reports real vertex / triangle / bone
//   counts and the model-local bounds, then frames a camera to those bounds and
//   renders the bind-pose mesh to a PNG via the software rasteriser -- proving
//   parseM2 + skinning produce non-degenerate, correctly-shaped geometry.
//
//   Usage:
//     wforge-m2dump <DataDir|WoWInstallDir|.> <archived\path.m2> [out.png] [--locale enUS]
//
//   Examples:
//     wforge-m2dump "D:/WoW" "world\azeroth\elwynn\passivedoodads\trees\elwynntreecanopy01.mdx" tree.png
//     wforge-m2dump "D:/WoW" "world\critter\birds\bird01.mdx" bird.png
//
//   The path may use the legacy ".mdx" name straight from an ADT's MMDX list;
//   the tool swaps it to ".m2" exactly as the engine does on lookup.
// ---------------------------------------------------------------------------
#include "mpq.hpp"
#include "wow_files.hpp"
#include "client_data.hpp"
#include "asset_loader.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"
#include "m2.hpp"
#include "m2_render.hpp"
#include "bounds.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void openChain(wf::MpqManager& mpq, const fs::path& dataDir, const std::string& locale) {
    wf::mountWowClient(mpq, dataDir, locale, [](const std::string& path, bool ok) {
        const fs::path p = path;
        if (ok) std::printf("  + %s\n", p.filename().string().c_str());
        else    std::printf("  ! failed to open %s\n", path.c_str());
    });
}

// MDDF/MMDX name models by their legacy ".mdx"/".mdl" extension, but vanilla
// MPQs store the converted ".m2". Swap exactly as AssetLoader::model() does.
std::string m2PathSwap(const std::string& path) {
    auto endsWith = [&](const char* ext) {
        size_t n = std::char_traits<char>::length(ext);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(path[path.size() - n + i])) != ext[i])
                return false;
        return true;
    };
    if (endsWith(".mdx") || endsWith(".mdl"))
        return path.substr(0, path.size() - 4) + ".m2";
    return path;
}

int renderModel(const wf::MpqManager& mpq, const std::string& rawPath,
                const std::string& outPng) {
    const std::string path = m2PathSwap(rawPath);
    std::vector<uint8_t> buf;
    if (!mpq.readFile(path, buf)) {
        std::fprintf(stderr, "M2 not found in archives: %s (from %s)\n",
                     path.c_str(), rawPath.c_str());
        return 1;
    }
    std::printf("# M2 %s  (%zu bytes)\n", path.c_str(), buf.size());

    wf::M2Model m;
    try {
        m = wf::parseM2(buf);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "M2 parse error: %s\n", e.what());
        return 1;
    }

    // ---- structural report (static mesh) ----
    const size_t tris = m.triangles.size() / 3;
    std::printf("#   version=0x%X name=\"%s\"\n", m.version, m.name.c_str());
    std::printf("#   vertices=%zu  vertexLookup=%zu  triangleIndices=%zu (=%zu tris)  "
                "submeshes=%zu  textures=%zu\n",
                m.vertices.size(), m.vertexLookup.size(), m.triangles.size(), tris,
                m.submeshes.size(), m.textures.size());
    for (size_t i = 0; i < m.submeshes.size(); ++i) {
        const wf::M2Submesh& s = m.submeshes[i];
        std::printf("#     submesh[%zu] id=%u vtx[%u..+%u] idx[%u..+%u]\n",
                    i, s.id, s.vertexStart, s.vertexCount, s.indexStart, s.indexCount);
    }
    for (size_t i = 0; i < m.textures.size(); ++i)
        std::printf("#     tex[%zu] type=%u \"%s\"\n",
                    i, (i < m.textureTypes.size() ? m.textureTypes[i] : 0u),
                    m.textures[i].c_str());

    // ---- sanity: every triangle index must resolve to a real global vertex ----
    size_t badLookup = 0, badTri = 0;
    for (uint16_t li : m.vertexLookup) if (li >= m.vertices.size()) ++badLookup;
    for (uint16_t ti : m.triangles)    if (ti >= m.vertexLookup.size()) ++badTri;
    if (badLookup) std::printf("#   ! %zu vertexLookup entries out of range\n", badLookup);
    if (badTri)    std::printf("#   ! %zu triangle indices out of range\n", badTri);

    // ---- bone-weight sanity over the global vertex pool ----
    {
        size_t zeroW = 0; uint8_t maxBone = 0;
        for (const wf::M2Vertex& v : m.vertices) {
            int sum = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
            if (sum == 0) ++zeroW;
            for (int k = 0; k < 4; ++k) if (v.boneWeights[k]) maxBone = std::max(maxBone, v.boneIndices[k]);
        }
        std::printf("#   bone-weights: %zu/%zu verts unweighted; max referenced bone index=%u\n",
                    zeroW, m.vertices.size(), maxBone);
    }

    // ---- animation: sequences + bones (verifies kSeqStride/kBoneStride/etc.) ----
    try {
        wf::M2Animation anim = wf::parseM2Animation(buf);
        size_t bonesWithKeys = 0, parentsBad = 0, pivotWild = 0;
        for (const wf::M2BoneRaw& b : anim.bones) {
            if (!b.translation.values.empty() || !b.rotation.values.empty() ||
                !b.scale.values.empty()) ++bonesWithKeys;
            if (b.parent >= (int)anim.bones.size()) ++parentsBad;
            if (!(std::abs(b.pivot.x) < 1e6f && std::abs(b.pivot.y) < 1e6f &&
                  std::abs(b.pivot.z) < 1e6f)) ++pivotWild;
        }
        std::printf("#   sequences=%zu  bones=%zu (animated=%zu, badParents=%zu, "
                    "wildPivots=%zu)\n",
                    anim.sequences.size(), anim.bones.size(), bonesWithKeys,
                    parentsBad, pivotWild);
        if (!anim.sequences.empty()) {
            const wf::M2Sequence& s0 = anim.sequences[0];
            std::printf("#     seq[0] id=%u sub=%u length=%ums flags=0x%X\n",
                        s0.id, s0.subId, s0.length, s0.flags);
        }
    } catch (const std::exception& e) {
        std::printf("#   (animation parse skipped: %s)\n", e.what());
    }

    // ---- skin the static bind pose ----
    wf::TexMesh mesh = wf::skinM2(m, {});
    const size_t meshTris = mesh.indices.size() / 3;
    std::printf("#   skinned bind pose: verts=%zu indices=%zu (=%zu tris)\n",
                mesh.vertices.size(), mesh.indices.size(), meshTris);

    // ---- model-local bounds (from the bind-pose vertex pool) ----
    wf::Aabb box = wf::modelBounds(m);
    if (!box.valid()) {
        std::fprintf(stderr, "M2 has degenerate/empty bounds -- nothing to render\n");
        return 1;
    }
    const wf::Vec3 lo = box.min, hi = box.max, c = box.center();
    std::printf("#   bounds min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f)  "
                "size=(%.3f, %.3f, %.3f)  radius=%.3f\n",
                lo.x, lo.y, lo.z, hi.x, hi.y, hi.z,
                hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, box.radius());

    if (mesh.indices.empty()) {
        std::fprintf(stderr, "M2 produced no triangles -- nothing to render\n");
        return 1;
    }
    if (outPng.empty()) return 0;   // report-only mode

    // ---- pick the model's first usable (type-0) texture; checker fallback else ----
    wf::AssetLoader loader(mpq);
    std::shared_ptr<const wf::Image> tex;
    for (const std::string& tn : m.textures)
        if (!tn.empty()) { tex = loader.texture(tn); break; }
    if (!tex) tex = loader.texture("__m2dump_missing__");   // magenta checker fallback

    // ---- frame a camera to the model bounds (mirrors renderTile) ----
    const float r = box.radius() + 0.1f;
    const int W = 1024, H = 768;
    wf::Framebuffer fb(W, H);
    fb.clear(wf::Rgba{ 24, 28, 40, 255 });
    // M2 local space is Z-up; view it from a 3/4 angle so the silhouette reads.
    wf::Mat4 view = wf::Mat4::lookAt(c + wf::Vec3{ r*1.4f, r*1.4f, r*1.0f }, c, { 0, 0, 1 });
    wf::Mat4 proj = wf::Mat4::perspective(45.0, double(W)/H, r*0.02 + 0.05, r*8.0 + 100.0);
    wf::rasterTexMesh(fb, mesh, proj * view, *tex, wf::Vec3{ 0.5f, 0.4f, 0.8f });

    if (!wf::writePng(fb.color, outPng)) {
        std::fprintf(stderr, "render: write failed\n");
        return 1;
    }
    std::printf("## m2 render: wrote %s  (%zu verts, %zu tris)\n",
                outPng.c_str(), mesh.vertices.size(), meshTris);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <DataDir|WoWInstallDir|.> <archived\\path.m2> [out.png] [--locale enUS]\n"
            "  The path may use the legacy .mdx/.mdl name; it is swapped to .m2.\n"
            "  Omit out.png for a report-only run (no render).\n", argv[0]);
        return 2;
    }

    const fs::path dataHint = argv[1];
    const std::string modelPath = argv[2];
    std::string outPng;
    std::string locale;

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--locale" && i + 1 < argc) locale = argv[++i];
        else if (!a.empty() && a[0] != '-')  outPng = a;
    }

    fs::path dataDir = wf::findDataDir(dataHint);
    if (dataDir.empty()) dataDir = dataHint;
    else if (dataDir != dataHint)
        std::printf("Auto-detected Data dir: %s\n", dataDir.string().c_str());

    if (locale.empty()) {
        locale = wf::detectLocale(dataDir);
        if (locale.empty()) locale = "enUS";
        else std::printf("Auto-detected locale: %s\n", locale.c_str());
    }

    std::printf("Opening archive chain from %s (locale %s):\n",
                dataDir.string().c_str(), locale.c_str());
    wf::MpqManager mpq;
    openChain(mpq, dataDir, locale);
    if (mpq.archiveCount() == 0) {
        std::fprintf(stderr, "No archives opened. Is the Data dir correct?\n");
        return 1;
    }
    std::printf("Opened %zu archive(s).\n\n", mpq.archiveCount());

    return renderModel(mpq, modelPath, outPng);
}
