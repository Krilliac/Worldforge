#include "asset_catalog.hpp"

#include <algorithm>
#include <cctype>

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

} // namespace wf
