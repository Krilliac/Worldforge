#pragma once
// ---------------------------------------------------------------------------
// AssetCatalog: derive browseable lists from a mounted MpqManager -- the data
// behind the editor's Map and Asset browsers. Pure (no ImGui): enumerate the
// client's maps (World\Maps\*\*.wdt) and its placeable models (*.m2 / root
// *.wmo), so a panel can present them and the loader can open the selection.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "mpq.hpp"

namespace wf {

struct MapInfo {
    std::string name;      // map directory name, e.g. "Azeroth"
    std::string wdtPath;   // archived WDT, e.g. "World\\Maps\\Azeroth\\Azeroth.wdt"
};

// All maps present in the mounted client, by enumerating World\Maps\*\*.wdt.
// Sorted by name; empty if no client is mounted.
std::vector<MapInfo> listMaps(const MpqManager& mpq);

enum class ModelKind { M2, Wmo };

// Browseable model files of a kind. WMO group files (Name_NNN.wmo) are excluded
// so only root WMOs appear -- those are what a placement references. Returns
// sorted, archived backslash paths.
std::vector<std::string> listModels(const MpqManager& mpq, ModelKind kind);

// True if `wmoPath` is a WMO group file (basename ends with _NNN.wmo, NNN digits)
// rather than a root WMO. Exposed for reuse/tests.
bool isWmoGroupFile(const std::string& wmoPath);

} // namespace wf
