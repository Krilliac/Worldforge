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
    // Group files end with _NNN.wmo (exactly three digits before the extension).
    const std::string ext = ".wmo";
    if (wmoPath.size() < ext.size() + 4) return false;
    size_t dot = wmoPath.size() - ext.size();
    // The 4 chars before ".wmo" must be '_' followed by three digits.
    size_t u = dot - 4;
    if (wmoPath[u] != '_') return false;
    for (size_t i = u + 1; i < dot; ++i)
        if (!std::isdigit((unsigned char)wmoPath[i])) return false;
    return true;
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

} // namespace wf
