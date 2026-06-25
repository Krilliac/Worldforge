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
#include "wow_files.hpp"   // Dbc

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

// A placeable thing resolved from the client's display DBCs: a display id and the
// on-disk model it maps to (.m2 doodad or .wmo), with a human label for browsing.
struct DisplayModel {
    uint32_t    displayId = 0;
    std::string model;       // archived path, .m2 or .wmo (extension normalised)
    std::string label;       // e.g. "Creature 1234  Cat.m2"
};

// Resolve CreatureDisplayInfo (displayId -> modelId) through CreatureModelData
// (modelId -> .mdx path) into placeable creature models (.mdx normalised to .m2).
// Sorted by display id; entries with no resolvable model are skipped.
std::vector<DisplayModel> listCreatureModels(const Dbc& displayInfo, const Dbc& modelData);

// Resolve GameObjectDisplayInfo (displayId -> model path) into placeable objects
// (.mdx normalised to .m2; .wmo left as-is). Sorted by display id.
std::vector<DisplayModel> listGameObjectModels(const Dbc& displayInfo);

// Resolve GroundEffectTexture -> GroundEffectDoodad into the detail doodads
// (grass/rocks) the client scatters per ground texture. Deduplicated by model,
// .mdx normalised to .m2. `displayId` carries the source doodad id. Sorted.
std::vector<DisplayModel> listGroundEffectModels(const Dbc& texture, const Dbc& doodad);

} // namespace wf
