#pragma once
// ---------------------------------------------------------------------------
// AssetLoader: the last mile -- read real client files from an MPQ chain and
// turn them into renderable data. Decodes BLP textures (cached), parses the WDT
// (which tiles exist) and ADT tiles (terrain + placements), and assembles a
// tile's textured terrain (MCNK/MCAL splat using the MTEX textures). The same
// MpqManager wforge-dump already uses; point it at a real Data dir.
//
// Tested headlessly by writing synthetic ADT/BLP files into an MPQ with
// writeMpqArchive, then loading them back -- no copyrighted assets needed.
// ---------------------------------------------------------------------------
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mpq.hpp"
#include "wow_files.hpp"
#include "terrain.hpp"
#include "wdl.hpp"
#include "terrain_render.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "m2.hpp"
#include "wmo.hpp"
#include "bounds.hpp"
#include "debugdraw.hpp"
#include "audio.hpp"
#include "lighting.hpp"

namespace wf {

// A bounded ambient lift from a WMO's MOLT interior lights: the intensity-weighted
// average light colour, scaled by `strength` and clamped to [0,1] per channel.
// Empty -> {0,0,0} (no change). Uses the light *colours* (a hue), not their
// absolute intensity magnitude, so it is robust to the uncertain MOLT intensity
// scale while still tinting interiors toward their lamps. Added on top of the
// zone light at render time, so a WMO with no MOLT renders exactly as before.
Vec3 wmoInteriorTint(const std::vector<WmoLight>& lights, float strength = 0.5f);

// MDDF doodad / MODF WMO placement -> world transform (translate * rotate *
// scale). Rotation is the stored Euler (degrees); the axis/order is the one
// item to confirm against a real tile (same risk class as MODF extent order).
Mat4 doodadMatrix(const DoodadDef& d);
Mat4 wmoMatrix(const WmoDef& w);

// Owns the meshes / textures / alpha maps for one rendered tile, so the
// TerrainLayer pointers stay valid while the tile is drawn.
// One chunk's translucent liquid (MCLQ) surface mesh + how to shade it: the
// per-type tint (water blue / magma orange / slime green, alpha in tint.a) and
// whether it glows (magma/slime skip directional shading). Drawn as a separate
// translucent pass after the opaque terrain.
struct LiquidSurface {
    Mesh       mesh;
    Rgba       tint{ 0, 0, 0, 0 };
    bool       emissive = false;
    LiquidType type     = LiquidType::None;
};

struct TileRender {
    std::vector<TexMesh>                      chunkMeshes;   // per MCNK
    std::vector<std::vector<AlphaMap>>        chunkAlphas;   // [chunk][overlay layer]
    std::vector<std::vector<TerrainLayer>>    chunkLayers;   // [chunk] -> splat layers
    std::vector<std::vector<uint8_t>>         chunkShadows;  // [chunk] raw MCSH (512B); empty = none
    std::vector<std::shared_ptr<const Image>> textures;      // MTEX, keep-alive
    std::vector<LiquidSurface>                liquids;        // translucent water surfaces
    float tiling = 8.0f;

    bool empty() const { return chunkMeshes.empty(); }
    bool hasLiquid() const { return !liquids.empty(); }
    // Rasterise every chunk's terrain splat into `fb` through `mvp`.
    void renderTerrain(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const;
    void renderTerrain(Framebuffer& fb, const Mat4& mvp, const ShadeLight& light) const;
    // Composite every liquid surface (translucent) -- call AFTER renderTerrain
    // and the opaque object passes so blending reads the correct background.
    void renderLiquid(Framebuffer& fb, const Mat4& mvp, Vec3 lightDir) const;
    void renderLiquid(Framebuffer& fb, const Mat4& mvp, const ShadeLight& light) const;
};

// A fully-populated tile: textured terrain + placed model instances + markers.
struct TileScene {
    TileRender terrain;
    std::vector<TexMesh>                      meshes;     // doodad/WMO meshes (owned)
    std::vector<std::shared_ptr<const Image>> textures;   // their textures (keep-alive)
    struct Inst { size_t mesh; size_t tex; Mat4 transform; bool blend = false;
                  Vec3 ambientBoost{0,0,0};        // WMO MOLT interior lift (0 = none)
                  bool bakedLight = false; };      // MOCV: the vertex colour IS the
                                                   // light -> render flat, not sun-lit
    std::vector<Inst> instances;            // placed M2 doodads
    std::vector<Inst> wmoRenderInstances;   // textured WMO parts (index into meshes/textures)

    // Placed WMO geometry (for per-triangle picking / wireframe). Kept as a plain
    // Mesh in WMO-local space + its MODF world transform + the placement id.
    struct WmoInst { Mesh mesh; Mat4 transform; uint32_t uniqueId; };
    std::vector<WmoInst> wmoInstances;

    DebugDraw markers;                                    // WMO/doodad placement markers

    // The parsed source tile, retained for editing + export. Edits (added
    // placements, terrain) apply to these and the ADT writer serialises them back
    // into a distributable patch. Empty for the procedural scene (hasSource=false).
    Adt                   sourceAdt;
    std::vector<MapChunk> sourceChunks;
    std::string           sourceMap;
    int                   sourceX = 0, sourceY = 0;
    bool                  hasSource = false;

    // Zone lighting (Light.dbc) resolved for this tile. Defaults reproduce the
    // legacy fixed light; AssetLoader::applyLighting() populates it from the
    // client's Light/LightParams/LightIntBand/LightFloatBand tables.
    ShadeLight light;                                     // opaque terrain/objects
    ShadeLight liquidLight{ {0.5f,0.4f,0.8f}, {0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f} };

    size_t doodadCount() const { return instances.size(); }
    size_t wmoCount()    const { return wmoInstances.size(); }
    // Legacy fixed-light render (lightDir only -> grey ambient/diffuse).
    void render(Framebuffer& fb, const Mat4& viewProj, Vec3 lightDir) const;
    // Zone-lit render: uses this tile's resolved `light` / `liquidLight`, with the
    // sun direction overridden to `lightDir`.
    void renderLit(Framebuffer& fb, const Mat4& viewProj, Vec3 lightDir) const;
};

class AssetLoader {
public:
    explicit AssetLoader(const MpqManager& mpq) : mpq_(mpq) {}

    // Decode a BLP by archived path (cached). Returns a magenta/checker fallback
    // if the file is missing or undecodable, so rendering never fails hard.
    std::shared_ptr<const Image> texture(const std::string& path);

    // Parse a map's WDT. Returns false (and leaves `out` default) if absent.
    bool loadWdt(const std::string& map, Wdt& out);

    // Parse a map's WDL low-res heightfield (one file per map, used to build the
    // distant-terrain LOD ring). Returns false (and leaves `out` default) if absent.
    bool loadWdl(const std::string& map, Wdl& out);

    // Parse one ADT tile: placements (Adt) + terrain chunks. False if absent.
    bool loadAdt(const std::string& map, int x, int y, Adt& adt, std::vector<MapChunk>& chunks);

    // Parse an M2 model by archived path (cached). nullptr if missing/malformed.
    std::shared_ptr<const M2Model> model(const std::string& path);

    // Model-local AABB of an M2 by path (bind-pose vertices, cached). Invalid
    // (default Aabb) if the model is missing/malformed -- callers fall back to a
    // sphere. The selectable bounds the bridge streams in EntityState.
    Aabb modelBounds(const std::string& path);

    // Load a whole WMO: the root file plus its `_NNN.wmo` group geometry
    // (cached). nullptr if the root is missing/malformed. Group files that fail
    // to read are skipped, so a partial WMO still loads.
    std::shared_ptr<const WmoModel> wmo(const std::string& path);

    // Extract a sound/music file's raw bytes from the chain (e.g. the sound
    // MPQ). False if the archived path is absent. Not cached -- audio buffers
    // are large and usually streamed once.
    bool sound(const std::string& path, std::vector<uint8_t>& out) const;

    // Extract + wrap as an AudioClip: WAV decoded to PCM, MP3 kept as encoded
    // bytes. An absent path yields an empty clip (codec None), never throws.
    AudioClip soundClip(const std::string& path) const;

    // Build a tile's textured terrain (MCNK meshes + MCAL splat layers from
    // MTEX). bigAlpha selects the 8-bit vs 4-bit MCAL form; it is governed by the
    // map's WDT MPHD "big alpha" flag (0x4). Pass std::nullopt (the default) to
    // auto-derive it from the WDT -- the correct behaviour for real tiles. Pass
    // an explicit value only to override (e.g. synthetic test fixtures).
    TileRender buildTile(const std::string& map, int x, int y,
                         std::optional<bool> bigAlpha = std::nullopt);

    // Build a fully-populated tile: terrain + every resolvable M2 doodad placed
    // by its MDDF transform; WMO placements get a marker (full WMO geometry is a
    // multi-file follow-up). Missing models are skipped, never fatal.
    TileScene buildTileScene(const std::string& map, int x, int y,
                             std::optional<bool> bigAlpha = std::nullopt);

    // Resolve zone lighting for a tile from a prebuilt LightDatabase and stamp it
    // into `ts.light` / `ts.liquidLight`. Samples the world position at the tile's
    // centre (block indices x,y) at `dayTick` (noon by default). A no-op (keeps the
    // default grey light) when the database is empty or has no matching sky.
    static void applyLighting(TileScene& ts, const LightDatabase& lights,
                              uint32_t mapId, int x, int y, float dayTick = kNoonTick);

    // Rebuild ts.terrain from its retained source ADT + (possibly edited) MCNK
    // chunks. Used by the editor after a terrain sculpt mutates ts.sourceChunks:
    // the height edits live in the authoritative data model, and this re-meshes
    // the derived render/pick geometry to match. Returns false (no-op) for a
    // scene with no retained source (hasSource == false, e.g. the procedural
    // scene). Leaves doodads/WMOs/placements and ts.light untouched -- the caller
    // re-applies applyLighting() as usual on the next frame.
    bool rebuildTileTerrain(TileScene& ts);

    // Export the edited tile as a patched ADT: re-reads the original tile bytes
    // from the archive chain and overwrites only the MCVT terrain heights from
    // ts.sourceChunks (the surgical writeAdtHeights path), so the result is a
    // valid ADT identical to the original except for sculpted heights. Returns
    // the bytes, or empty if the scene has no retained source / the original
    // can't be re-read. Throws if the retained chunks don't match the re-read
    // buffer (offset overrun) -- the caller should guard with try/catch.
    std::vector<uint8_t> exportTileAdt(const TileScene& ts);

    // --- interactive placement (click-to-place into a live scene) ------------
    // Place an M2 doodad `m2Path` into `ts` at world position `world`, rotated
    // `rotZ` radians about Z and uniformly scaled. Appends the mesh/texture/
    // instance so the doodad renders next frame. Returns the new ts.instances
    // index, or SIZE_MAX if the model can't be resolved (nothing is added).
    size_t placeDoodad(TileScene& ts, const std::string& m2Path, Vec3 world,
                       float rotZ = 0.0f, float scale = 1.0f);

    // Place a WMO `wmoPath` into `ts` at `world` (rotated `rotZ` about Z). Adds
    // the textured render parts + a pickable instance (uniqueId), mirroring how
    // buildTileScene places map objects. Returns the new ts.wmoInstances index,
    // or SIZE_MAX if the WMO can't be resolved.
    size_t placeWmo(TileScene& ts, const std::string& wmoPath, Vec3 world,
                    float rotZ = 0.0f, uint32_t uniqueId = 0);

private:
    static std::string wdtPath(const std::string& map);
    static std::string wdlPath(const std::string& map);
    static std::string adtPath(const std::string& map, int x, int y);

    TileRender buildTerrain(const Adt& adt, const std::vector<MapChunk>& chunks,
                            int x, int y, bool bigAlpha);

    // Lazily-built 8x8 magenta/grey checker, returned for any missing/undecodable
    // texture so rendering never fails hard.
    std::shared_ptr<const Image> fallback();

    // Resolve the effective bigAlpha for a map: honour an explicit override, else
    // derive it from the map's WDT MPHD flag (0x4). Cached per map; defaults to
    // false (vanilla packed 4-bit) if the WDT is missing/unreadable.
    bool resolveBigAlpha(const std::string& map, std::optional<bool> override_);

    const MpqManager& mpq_;
    std::unordered_map<std::string, std::shared_ptr<const Image>>   texCache_;
    std::unordered_map<std::string, std::shared_ptr<const M2Model>> modelCache_;
    std::unordered_map<std::string, Aabb>                          boundsCache_;
    std::unordered_map<std::string, std::shared_ptr<const WmoModel>> wmoCache_;
    std::unordered_map<std::string, bool>                          bigAlphaCache_;
    std::shared_ptr<const Image> fallback_;
};

} // namespace wf
