#pragma once
// ---------------------------------------------------------------------------
// AssetCatalog: derive browseable lists from a mounted MpqManager -- the data
// behind the editor's Map and Asset browsers. Pure (no ImGui): enumerate the
// client's maps (World\Maps\*\*.wdt) and its placeable models (*.m2 / root
// *.wmo), so a panel can present them and the loader can open the selection.
// ---------------------------------------------------------------------------
#include <map>
#include <string>
#include <unordered_map>
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
// rather than a root WMO. Extension matched case-insensitively (MPQ paths mix
// casings). Exposed for reuse/tests -- the asset browser offers it as a
// browse-mode toggle so only placeable root WMOs list.
bool isWmoGroupFile(const std::string& wmoPath);

// --- AssetTree: the listfile folder hierarchy behind the asset browser -------
// Built ONCE from flat archived paths ("World\Azeroth\...\Tree.m2"). Directory
// children are keyed CASE-INSENSITIVELY (MPQ paths are case-insensitive, so
// "WORLD\" and "World\" merge into one node) with the first-seen casing kept as
// the display name. Leaf files are stored as indices into the source path
// vector, so the tree adds no second string pool and two genuinely distinct
// paths that differ only by case both keep their entries.
struct AssetTreeNode {
    std::string name;                            // display casing (first seen)
    std::map<std::string, AssetTreeNode> dirs;   // key = lowercased component
    std::vector<int> fileIndices;                // files directly in this dir
};

struct AssetTree {
    AssetTreeNode root;   // unnamed; holds top-level dirs + directory-less files
};

// Build the folder tree from a flat path list (indices refer into `paths`).
// Empty/degenerate paths are skipped; an empty list yields an empty tree.
AssetTree buildAssetTree(const std::vector<std::string>& paths);

// Every file index in `node`'s subtree (its own files + all descendants'),
// sorted ascending -- the "scope the browser to this folder" prefix set.
std::vector<int> subtreePaths(const AssetTreeNode& node);

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

// Runtime displayId -> model resolver. The list* functions above enumerate the
// whole catalog for the browsers; this indexes the same resolution as an in-
// memory map so a *server-streamed* entity (SMSG_UPDATE_OBJECT carries a
// UNIT_FIELD_DISPLAYID / GO display id, see net/update_object.hpp) resolves to
// its on-disk model in O(1) as objects appear. Same shape as the vanilla client
// keeping a typed static-DBC view keyed by id (see
// ../../WoW-RE-Research/client/alpha-053-architecture.md, DBClient WowClientDB).
//
// Build once from the mounted client's DBCs; queries are const and thread-safe.
// Creatures and game objects are separate id spaces (different DBCs), so they are
// resolved through separate calls. A miss returns an empty string.
class DisplayResolver {
public:
    // Index creature display ids: CreatureDisplayInfo(displayId->modelId) joined
    // through CreatureModelData(modelId->path). Safe to call on empty DBCs.
    void buildCreatures(const Dbc& displayInfo, const Dbc& modelData);
    // Index game-object display ids: GameObjectDisplayInfo(displayId->path).
    void buildGameObjects(const Dbc& displayInfo);

    // Resolve a creature / game-object display id to its archived model path
    // (.m2 or .wmo). Returns an empty string when the id is unknown.
    const std::string& creatureModel(uint32_t displayId) const;
    const std::string& gameObjectModel(uint32_t displayId) const;

    bool   hasCreature(uint32_t displayId) const { return creatures_.count(displayId) != 0; }
    bool   hasGameObject(uint32_t displayId) const { return gameObjects_.count(displayId) != 0; }
    size_t creatureCount()   const { return creatures_.size(); }
    size_t gameObjectCount() const { return gameObjects_.size(); }

private:
    std::unordered_map<uint32_t, std::string> creatures_;
    std::unordered_map<uint32_t, std::string> gameObjects_;
    std::string empty_;   // returned by reference on a miss
};

} // namespace wf
