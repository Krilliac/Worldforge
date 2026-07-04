#pragma once
// ---------------------------------------------------------------------------
// Baked-minimap index: the client's pre-rendered minimap tiles are stored under
// hashed names, and textures\Minimap\md5translate.trs (a plain-text table) maps
// each logical tile "<Map>\map<xx>_<yy>.blp" to its stored ".blp" name. This is
// what lets an editor show the REAL minimap art (per-ADT-tile) rather than a
// heightfield silhouette (see minimap_demo.cpp for the WDL-derived fallback).
//
// The .trs is organised as `dir: <section>` headers followed by
// `<logical>\t<stored>` lines; vanilla writes the full logical path on the left
// (e.g. "Azeroth\map32_48.blp"), older builds (0.5.3, md5translate.txt) do the
// same. We tolerate both a full-path left column and a bare "map<xx>_<yy>.blp"
// (prefixed by the current `dir:` section). Format cross-checked against the real
// 0.5.3 md5translate.txt; storage location VERIFY-FLAGGED for 1.12.1.
//
// Pure text -> table (no MPQ / BLP): a mounted client feeds the .trs bytes in,
// then decodes the resolved stored .blp via the existing blp path.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <unordered_map>

namespace wf {

// Parsed md5translate table: logical tile key -> stored blp name.
class MinimapIndex {
public:
    // Parse a md5translate.trs / .txt text buffer. Repeated logical keys keep the
    // last mapping (matches the client reading top-to-bottom). Blank lines and
    // non-`dir:` lines without a tab separator are ignored (the format is strictly
    // tab-delimited; map/stored names never contain spaces).
    static MinimapIndex parse(const std::string& text);

    // Stored blp name for a map tile (column x, row y), or "" if absent. The
    // stored name is what lives under textures\Minimap\ (see storedPath()).
    // Axis order (x = column) is VERIFY-FLAGGED, consistent with the ADT
    // tile-filename convention used elsewhere in the engine.
    std::string tile(const std::string& map, int x, int y) const;

    // Stored blp name for an already-built logical key (see tileKey()).
    std::string resolve(const std::string& logicalKey) const;

    size_t size() const { return byLogical_.size(); }
    bool   empty() const { return byLogical_.empty(); }

    // The logical key for a tile: lowercased "<map>\map<xx>_<yy>.blp" with the
    // coordinates zero-padded to two digits (matches the on-disk naming).
    static std::string tileKey(const std::string& map, int x, int y);

    // The archived path a stored name resolves to: "textures\Minimap\<stored>".
    static std::string storedPath(const std::string& storedName);

private:
    std::unordered_map<std::string, std::string> byLogical_;
};

}  // namespace wf
