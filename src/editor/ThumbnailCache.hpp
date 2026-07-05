#pragma once
// ---------------------------------------------------------------------------
// ThumbnailCache: 96x96 software-rasterised asset previews for the browser's
// grid view, with a PNG disk cache. The key is fnv1a64(archived path +
// uncompressed size) so a repacked/edited file (size change) invalidates its
// thumbnail by simply missing under the new key. get() never blocks on
// rendering: an unknown asset is queued Pending and generateBudget(n) renders
// at most n queued thumbnails per call (the caller's per-frame budget) via the
// SOFTWARE rasterizer -- a mesh source gets a canned fit-to-AABB orbit camera
// (~30 degrees elevation), an image source (BLP) is scaled to fit. A loader
// failure is cached as Failed (negative cache) so it isn't retried every
// frame. The loader and cache directory are injected, so tests drive the whole
// path with synthetic meshes + a scratch dir and never touch an MPQ.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

#include "image.hpp"
#include "raster.hpp"   // TexMesh

namespace wf {
class AssetLoader;
class MpqManager;
} // namespace wf

namespace wf::editor {

enum class ThumbStatus { Pending, Ready, Failed };

// get() result: `image` is non-null only when Ready (owned by the cache; valid
// until the cache is destroyed).
struct ThumbResult {
    ThumbStatus  status = ThumbStatus::Pending;
    const Image* image  = nullptr;
};

// What the injected loader yields for one asset: a mesh (+ its texture) to
// render with the canned orbit camera, or -- when `mesh` is empty -- a decoded
// image (the BLP path) to scale directly. ok=false means the asset failed to
// load (cached as Failed).
struct ThumbSource {
    bool    ok = false;
    TexMesh mesh;    // non-empty -> rasterise with the fit-to-AABB camera
    Image   image;   // the mesh's texture, or the image itself when mesh empty
};

// fnv1a64 of `path` + '|' + decimal `size`, as 16 lowercase hex chars -- the
// disk-cache file stem. Pure; exposed for tests/tooling.
std::string thumbKeyHash(const std::string& path, uint64_t size);

class ThumbnailCache {
public:
    using Loader = std::function<ThumbSource(const std::string& path)>;
    using SizeFn = std::function<uint64_t(const std::string& path)>;

    // `cacheDir` is created if absent; thumbnails persist there as <hash>.png.
    // `sizeFn` supplies the uncompressed size for the cache key WITHOUT loading
    // the asset (so a disk hit never invokes `loader`).
    ThumbnailCache(std::string cacheDir, SizeFn sizeFn, Loader loader, int dim = 96);

    // Look up `path`: Ready with its image (memory or disk hit), Failed (cached
    // negative), or Pending (queued for generateBudget). Never renders.
    ThumbResult get(const std::string& path);

    // Render at most `n` pending thumbnails (the per-frame budget). Each one is
    // loaded, rasterised, written to the disk cache, and flipped to Ready --
    // or flipped to Failed if the loader reports failure. Returns how many
    // pending entries were processed.
    int generateBudget(int n);

    // The on-disk PNG path for `path`'s current key (invokes sizeFn).
    std::string diskPathFor(const std::string& path) const;

    int    dim() const { return dim_; }
    size_t pendingCount() const { return pending_.size(); }

private:
    struct Entry {
        ThumbStatus status = ThumbStatus::Pending;
        Image       image;
        std::string diskPath;   // resolved once (sizeFn is not free)
    };

    std::string cacheDir_;
    SizeFn      sizeFn_;
    Loader      loader_;
    int         dim_;
    std::unordered_map<std::string, Entry> entries_;
    std::deque<std::string>                pending_;
};

// Production loader over the mounted client: dispatches by extension -- M2 via
// model()+skinM2 bind pose, WMO via wmo()+wmoRenderParts (groups merged), BLP
// via decodeBlpForSize direct. `assets`/`mpq` must outlive the returned fn.
ThumbnailCache::Loader makeAssetThumbLoader(AssetLoader& assets, const MpqManager& mpq,
                                            int dim = 96);

// Production size fn: the extracted file's byte size (cached per path -- the
// MPQ chain has no cheap size probe, so the first query extracts once).
ThumbnailCache::SizeFn makeMpqSizeFn(const MpqManager& mpq);

} // namespace wf::editor
