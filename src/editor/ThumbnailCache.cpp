#include "editor/ThumbnailCache.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "asset_loader.hpp"
#include "blp.hpp"        // decodeBlpForSize
#include "bounds.hpp"     // Aabb
#include "m2_render.hpp"  // skinM2
#include "math.hpp"
#include "mpq.hpp"
#include "wmo_render.hpp" // wmoRenderParts

namespace wf::editor {
namespace {

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

// ---- minimal PNG reader for OUR OWN cache files ---------------------------
// encodePng (image.cpp) writes 8-bit RGBA, filter 0 rows, and stored
// (uncompressed) zlib blocks -- this reads exactly that subset and rejects
// anything else, so a foreign/corrupt file is just a cache miss (re-render).
uint32_t get32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool inflateStored(const std::vector<uint8_t>& z, std::vector<uint8_t>& raw) {
    if (z.size() < 2) return false;
    size_t pos = 2;                             // skip the zlib header
    for (;;) {
        if (pos >= z.size()) return false;
        uint8_t hdr = z[pos++];
        if ((hdr >> 1) & 3) return false;       // only stored blocks (BTYPE 00)
        if (pos + 4 > z.size()) return false;
        uint16_t len  = uint16_t(z[pos] | (z[pos + 1] << 8));
        uint16_t nlen = uint16_t(z[pos + 2] | (z[pos + 3] << 8));
        pos += 4;
        if (uint16_t(~len) != nlen || pos + len > z.size()) return false;
        raw.insert(raw.end(), z.begin() + pos, z.begin() + pos + len);
        pos += len;
        if (hdr & 1) return true;               // BFINAL
    }
}

bool readCachedPng(const std::string& file, Image& out) {
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(sz > 0 ? (size_t)sz : 0);
    size_t got = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size() || bytes.size() < 8) return false;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (!std::equal(sig, sig + 8, bytes.begin())) return false;

    int w = 0, h = 0;
    std::vector<uint8_t> idat;
    size_t pos = 8;
    while (pos + 8 <= bytes.size()) {
        uint32_t len = get32be(&bytes[pos]);
        if (pos + 12 + len > bytes.size()) return false;
        const char* tag = (const char*)&bytes[pos + 4];
        const uint8_t* data = &bytes[pos + 8];
        if (std::memcmp(tag, "IHDR", 4) == 0) {
            if (len < 13) return false;
            w = (int)get32be(data); h = (int)get32be(data + 4);
            if (data[8] != 8 || data[9] != 6 || data[12] != 0) return false;  // 8-bit RGBA only
        } else if (std::memcmp(tag, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + len);
        } else if (std::memcmp(tag, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;   // len + tag + data + crc (crc trusted; our own file)
    }
    if (w <= 0 || h <= 0 || idat.empty()) return false;

    std::vector<uint8_t> raw;
    if (!inflateStored(idat, raw)) return false;
    const size_t stride = 1 + (size_t)w * 4;
    if (raw.size() != stride * (size_t)h) return false;

    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = &raw[(size_t)y * stride];
        if (row[0] != 0) return false;          // filter None only
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = row + 1 + (size_t)x * 4;
            img.at(x, y) = Rgba{ p[0], p[1], p[2], p[3] };
        }
    }
    out = std::move(img);
    return true;
}

// ---- thumbnail rendering ---------------------------------------------------
constexpr Rgba kThumbBackdrop{ 28, 30, 38, 255 };   // matches ModelViewerPanel

// Canned camera: orbit at ~30 degrees elevation, distance fit to the mesh AABB.
Image renderMeshThumb(const TexMesh& mesh, const Image& texture, int dim) {
    Framebuffer fb(dim, dim);
    fb.clear(kThumbBackdrop);

    Aabb box;
    for (const TexVertex& v : mesh.vertices) box.expand(v.position);
    if (!box.valid() || mesh.indices.empty()) return fb.color;

    const Vec3  c = box.center();
    const float r = std::max(box.radius(), 1e-3f);
    const float elev = 0.5236f, azim = 0.8f;         // ~30 deg up, 3/4 view
    const float dist = r * 2.4f;
    Vec3 eye = c + Vec3{ dist * std::cos(elev) * std::cos(azim),
                         dist * std::cos(elev) * std::sin(azim),
                         dist * std::sin(elev) };
    Mat4 view = Mat4::lookAt(eye, c, Vec3{0, 0, 1});
    Mat4 proj = Mat4::perspective(45.0, 1.0, std::max(0.05f, dist - r * 2.0f), dist + r * 4.0f);

    if (texture.width > 0) {
        rasterTexMesh(fb, mesh, proj * view, texture, Vec3{0.4f, 0.3f, 0.85f});
    } else {
        Image grey(1, 1);
        grey.at(0, 0) = Rgba{ 170, 170, 170, 255 };
        rasterTexMesh(fb, mesh, proj * view, grey, Vec3{0.4f, 0.3f, 0.85f});
    }
    return fb.color;
}

// Nearest-scale `src` to fit a dim x dim cell (aspect preserved, centred).
Image scaleImageThumb(const Image& src, int dim) {
    Image out(dim, dim);
    for (Rgba& p : out.pixels) p = kThumbBackdrop;
    if (src.width <= 0 || src.height <= 0) return out;

    float scale = std::min((float)dim / src.width, (float)dim / src.height);
    int w = std::max(1, (int)std::lround(src.width  * scale));
    int h = std::max(1, (int)std::lround(src.height * scale));
    int ox = (dim - w) / 2, oy = (dim - h) / 2;
    for (int y = 0; y < h; ++y) {
        int sy = std::min(src.height - 1, (int)((y + 0.5f) * src.height / h));
        for (int x = 0; x < w; ++x) {
            int sx = std::min(src.width - 1, (int)((x + 0.5f) * src.width / w));
            out.at(ox + x, oy + y) = src.at(sx, sy);
        }
    }
    return out;
}

bool endsWithCI(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
    return true;
}

} // namespace

std::string thumbKeyHash(const std::string& path, uint64_t size) {
    uint64_t h = fnv1a64(path + '|' + std::to_string(size));
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

ThumbnailCache::ThumbnailCache(std::string cacheDir, SizeFn sizeFn, Loader loader, int dim)
    : cacheDir_(std::move(cacheDir)), sizeFn_(std::move(sizeFn)),
      loader_(std::move(loader)), dim_(dim) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);   // best-effort
}

std::string ThumbnailCache::diskPathFor(const std::string& path) const {
    return cacheDir_ + "/" + thumbKeyHash(path, sizeFn_ ? sizeFn_(path) : 0) + ".png";
}

ThumbResult ThumbnailCache::get(const std::string& path) {
    auto it = entries_.find(path);
    if (it == entries_.end()) {
        Entry e;
        e.diskPath = diskPathFor(path);
        if (readCachedPng(e.diskPath, e.image)) {
            e.status = ThumbStatus::Ready;        // disk hit: no render needed
        } else {
            e.status = ThumbStatus::Pending;
            pending_.push_back(path);
        }
        it = entries_.emplace(path, std::move(e)).first;
    }
    const Entry& e = it->second;
    return { e.status, e.status == ThumbStatus::Ready ? &e.image : nullptr };
}

int ThumbnailCache::generateBudget(int n) {
    int done = 0;
    while (done < n && !pending_.empty()) {
        std::string path = std::move(pending_.front());
        pending_.pop_front();
        auto it = entries_.find(path);
        if (it == entries_.end() || it->second.status != ThumbStatus::Pending) continue;
        Entry& e = it->second;

        ThumbSource src = loader_ ? loader_(path) : ThumbSource{};
        if (!src.ok) {
            e.status = ThumbStatus::Failed;       // negative cache: never retried
            ++done;
            continue;
        }
        e.image = src.mesh.vertices.empty() ? scaleImageThumb(src.image, dim_)
                                            : renderMeshThumb(src.mesh, src.image, dim_);
        e.status = ThumbStatus::Ready;
        writePng(e.image, e.diskPath);            // best-effort disk cache
        ++done;
    }
    return done;
}

ThumbnailCache::Loader makeAssetThumbLoader(AssetLoader& assets, const MpqManager& mpq,
                                            int dim) {
    return [&assets, &mpq, dim](const std::string& path) -> ThumbSource {
        ThumbSource out;
        if (endsWithCI(path, ".blp")) {
            std::vector<uint8_t> buf;
            if (!mpq.readFile(path, buf)) return out;
            try { out.image = decodeBlpForSize(buf, dim); } catch (...) { return out; }
            out.ok = out.image.width > 0;
            return out;
        }
        if (endsWithCI(path, ".m2") || endsWithCI(path, ".mdx")) {
            std::shared_ptr<const M2Model> mdl = assets.model(path);
            if (!mdl) return out;
            out.mesh = skinM2(*mdl, {});          // static bind pose
            if (!mdl->textures.empty() && !mdl->textures[0].empty())
                out.image = *assets.texture(mdl->textures[0]);
            out.ok = !out.mesh.vertices.empty();
            return out;
        }
        if (endsWithCI(path, ".wmo")) {
            std::shared_ptr<const WmoModel> w = assets.wmo(path);
            if (!w) return out;
            for (const WmoRenderPart& part : wmoRenderParts(*w)) {
                uint32_t base = (uint32_t)out.mesh.vertices.size();
                out.mesh.vertices.insert(out.mesh.vertices.end(),
                                         part.mesh.vertices.begin(), part.mesh.vertices.end());
                for (uint32_t idx : part.mesh.indices) out.mesh.indices.push_back(base + idx);
                if (out.image.width == 0 && !part.texture.empty())
                    out.image = *assets.texture(part.texture);   // first diffuse
            }
            out.ok = !out.mesh.vertices.empty();
            return out;
        }
        return out;   // unknown extension -> Failed
    };
}

ThumbnailCache::SizeFn makeMpqSizeFn(const MpqManager& mpq) {
    auto cache = std::make_shared<std::unordered_map<std::string, uint64_t>>();
    return [&mpq, cache](const std::string& path) -> uint64_t {
        auto it = cache->find(path);
        if (it != cache->end()) return it->second;
        std::vector<uint8_t> buf;
        uint64_t size = mpq.readFile(path, buf) ? (uint64_t)buf.size() : 0;
        (*cache)[path] = size;
        return size;
    };
}

} // namespace wf::editor
