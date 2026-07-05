#include "asset_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "dbc_defs.hpp"   // creature/gameobject display readers + normalizeModelPath

namespace wf {
namespace {

// Split an archived backslash path into components ("World\\Maps\\Azeroth\\X.wdt"
// -> {"World","Maps","Azeroth","X.wdt"}).
std::vector<std::string> splitPath(const std::string& p) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '\\' || p[i] == '/') {
            if (i > start) out.push_back(p.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

bool iequalsAscii(const std::string& a, const char* b) {
    size_t n = 0;
    for (; n < a.size() && b[n]; ++n)
        if (std::tolower((unsigned char)a[n]) != std::tolower((unsigned char)b[n])) return false;
    return n == a.size() && b[n] == '\0';
}

std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

} // namespace

std::vector<MapInfo> listMaps(const MpqManager& mpq) {
    std::vector<MapInfo> maps;
    for (const std::string& path : mpq.listFiles("*.wdt")) {
        // Expect World\Maps\<Name>\<file>.wdt; the map name is the directory.
        std::vector<std::string> parts = splitPath(path);
        if (parts.size() < 4) continue;
        if (!iequalsAscii(parts[0], "World") || !iequalsAscii(parts[1], "Maps")) continue;
        maps.push_back({ parts[parts.size() - 2], path });
    }
    std::sort(maps.begin(), maps.end(),
              [](const MapInfo& a, const MapInfo& b) { return a.name < b.name; });
    return maps;
}

bool isWmoGroupFile(const std::string& wmoPath) {
    // Group files end with _NNN.wmo (exactly three digits before the extension;
    // the extension is matched case-insensitively -- MPQ paths mix casings).
    const char* ext = ".wmo";
    const size_t extLen = 4;
    if (wmoPath.size() < extLen + 4) return false;
    size_t dot = wmoPath.size() - extLen;
    for (size_t i = 0; i < extLen; ++i)
        if (std::tolower((unsigned char)wmoPath[dot + i]) != ext[i]) return false;
    // The 4 chars before ".wmo" must be '_' followed by three digits.
    size_t u = dot - 4;
    if (wmoPath[u] != '_') return false;
    for (size_t i = u + 1; i < dot; ++i)
        if (!std::isdigit((unsigned char)wmoPath[i])) return false;
    return true;
}

AssetTree buildAssetTree(const std::vector<std::string>& paths) {
    AssetTree tree;
    for (int i = 0; i < (int)paths.size(); ++i) {
        std::vector<std::string> parts = splitPath(paths[i]);
        if (parts.empty()) continue;                      // empty/degenerate path
        AssetTreeNode* node = &tree.root;
        for (size_t d = 0; d + 1 < parts.size(); ++d) {   // all but the file name
            AssetTreeNode& child = node->dirs[lowerAscii(parts[d])];
            if (child.name.empty()) child.name = parts[d];   // first-seen casing
            node = &child;
        }
        node->fileIndices.push_back(i);   // a path with no directory -> root
    }
    return tree;
}

namespace {
void collectSubtree(const AssetTreeNode& node, std::vector<int>& out) {
    out.insert(out.end(), node.fileIndices.begin(), node.fileIndices.end());
    for (const auto& [key, child] : node.dirs) collectSubtree(child, out);
}
} // namespace

std::vector<int> subtreePaths(const AssetTreeNode& node) {
    std::vector<int> out;
    collectSubtree(node, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> listModels(const MpqManager& mpq, ModelKind kind) {
    if (kind == ModelKind::M2)
        return mpq.listFiles("*.m2");

    std::vector<std::string> out;
    for (const std::string& w : mpq.listFiles("*.wmo"))
        if (!isWmoGroupFile(w)) out.push_back(w);   // root WMOs only
    return out;
}

namespace {
std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}
} // namespace

std::vector<DisplayModel> listCreatureModels(const Dbc& displayInfo, const Dbc& modelData) {
    // Index CreatureModelData: modelId -> on-disk model path (.mdx -> .m2).
    std::unordered_map<uint32_t, std::string> models;
    for (uint32_t r = 0; r < modelData.recordCount(); ++r) {
        CreatureModelDataEntry m = creatureModelDataEntry(modelData, r);
        if (!m.modelPath.empty()) models[m.id] = normalizeModelPath(m.modelPath);
    }
    std::vector<DisplayModel> out;
    for (uint32_t r = 0; r < displayInfo.recordCount(); ++r) {
        CreatureDisplayInfoEntry d = creatureDisplayInfoEntry(displayInfo, r);
        auto it = models.find(d.modelId);
        if (it == models.end() || it->second.empty()) continue;   // unresolved -> skip
        out.push_back({ d.id, it->second,
                        "Creature " + std::to_string(d.id) + "  " + baseName(it->second) });
    }
    std::sort(out.begin(), out.end(),
              [](const DisplayModel& a, const DisplayModel& b) { return a.displayId < b.displayId; });
    return out;
}

std::vector<DisplayModel> listGameObjectModels(const Dbc& displayInfo) {
    std::vector<DisplayModel> out;
    for (uint32_t r = 0; r < displayInfo.recordCount(); ++r) {
        GameObjectDisplayInfoEntry g = gameObjectDisplayInfoEntry(displayInfo, r);
        if (g.modelName.empty()) continue;
        std::string model = normalizeModelPath(g.modelName);
        out.push_back({ g.id, model,
                        "GameObject " + std::to_string(g.id) + "  " + baseName(model) });
    }
    std::sort(out.begin(), out.end(),
              [](const DisplayModel& a, const DisplayModel& b) { return a.displayId < b.displayId; });
    return out;
}

std::vector<DisplayModel> listGroundEffectModels(const Dbc& texture, const Dbc& doodad) {
    // Index GroundEffectDoodad: id -> normalised model path.
    std::unordered_map<uint32_t, std::string> models;
    for (uint32_t r = 0; r < doodad.recordCount(); ++r) {
        GroundEffectDoodadEntry d = groundEffectDoodadEntry(doodad, r);
        if (!d.modelPath.empty()) models[d.id] = normalizeModelPath(d.modelPath);
    }
    std::vector<DisplayModel> out;
    std::unordered_map<std::string, bool> seen;   // dedup by model
    for (uint32_t r = 0; r < texture.recordCount(); ++r) {
        GroundEffectTextureEntry t = groundEffectTextureEntry(texture, r);
        for (uint32_t did : t.doodadIds) {
            if (did == 0) continue;
            auto it = models.find(did);
            if (it == models.end() || it->second.empty() || seen.count(it->second)) continue;
            seen[it->second] = true;
            out.push_back({ did, it->second, "Detail " + std::to_string(did) + "  " + baseName(it->second) });
        }
    }
    std::sort(out.begin(), out.end(),
              [](const DisplayModel& a, const DisplayModel& b) { return a.displayId < b.displayId; });
    return out;
}

// --- DisplayResolver: index the same joins the list* functions produce, keyed
//     by display id for O(1) runtime lookup. Reusing list* keeps the resolution
//     semantics (path normalisation, unresolved-skip) identical to the browser.
void DisplayResolver::buildCreatures(const Dbc& displayInfo, const Dbc& modelData) {
    for (const DisplayModel& d : listCreatureModels(displayInfo, modelData))
        creatures_[d.displayId] = d.model;
}

void DisplayResolver::buildGameObjects(const Dbc& displayInfo) {
    for (const DisplayModel& d : listGameObjectModels(displayInfo))
        gameObjects_[d.displayId] = d.model;
}

const std::string& DisplayResolver::creatureModel(uint32_t displayId) const {
    auto it = creatures_.find(displayId);
    return it == creatures_.end() ? empty_ : it->second;
}

const std::string& DisplayResolver::gameObjectModel(uint32_t displayId) const {
    auto it = gameObjects_.find(displayId);
    return it == gameObjects_.end() ? empty_ : it->second;
}

} // namespace wf
