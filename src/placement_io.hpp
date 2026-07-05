#pragma once
// ---------------------------------------------------------------------------
// Scene placement persistence + placement authoring core.
//
// Part 1 (persistence): save/load the editor's list of object placements (M2
// doodads and WMO buildings) to/from a stable line-based text format. This is
// the storage layer behind the editor's "Save/Load Scene" commands --
// deliberately GPU- and ImGui-independent so it is fully unit-testable
// headless. The format is a one-line header ("WFSCENE 1") followed by one
// tab-separated record per placement, which keeps it diffable and trivially
// round-trippable.
//
// Part 2 (authoring): the operations an object-placement editor needs on the
// parsed MDDF/MODF records (DoodadDef / WmoDef from wow_files.hpp), all in
// their NATIVE stored encoding (rotation Euler degrees, M2 scale uint16 with
// 1024 == 1.0): map-wide uniqueId allocation and de-duplication, a typed
// clipboard with paste modes and deterministic jitter, snap-to-ground,
// replace-preserving-transform, and a CSV sidecar export. Terrain queries are
// passed in as callbacks so terrain code never leaks into this file and every
// operation stays headless-testable.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "math.hpp"
#include "wow_files.hpp"

namespace wf {

// A single placed object in the scene. `model` is the archived path (.m2 or
// .wmo); `pos` is world space; `rotZ` is the yaw in degrees; `scale` is a
// uniform factor. `isWmo` selects the building vs. doodad code paths.
struct ScenePlacement {
    std::string model;          // archived path, .m2 or .wmo
    Vec3        pos;
    float       rotZ  = 0.0f;
    float       scale = 1.0f;
    bool        isWmo = false;
};

// Serialise to a stable line-based text format (one placement per line).
// Begins with the header line "WFSCENE 1". Each record is
// "<M2|WMO>\t<model>\t<x>\t<y>\t<z>\t<rotZ>\t<scale>". Round-trips exactly for
// representable floats.
std::string saveScene(const std::vector<ScenePlacement>& placements);

// Parse the text produced by saveScene. Tolerant of blank lines / trailing
// whitespace; ignores malformed lines (too few fields). Returns the placements
// (empty on garbage / empty input).
std::vector<ScenePlacement> loadScene(const std::string& text);

// ===========================================================================
// Terrain sampling callbacks. Both operate in WORLD space (coords.hpp:
// +X north, +Y west, +Z up). HeightFn returns the ground height at (x, y);
// NormalFn the unit surface normal there. The caller supplies them (terrain,
// or a synthetic function in tests) -- placement code never samples terrain
// itself.
// ===========================================================================
using HeightFn = std::function<float(float x, float y)>;
using NormalFn = std::function<Vec3(float x, float y)>;

// ===========================================================================
// UidAllocator -- MDDF/MODF uniqueId must be unique across the ENTIRE map
// (all tiles): the client de-duplicates objects that straddle tile borders by
// this id, so a duplicate makes one instance vanish or lose collision. Scan
// every loaded tile's placements into one allocator, then mint new ids with
// next(). Persisting the per-map max between sessions is the caller's job --
// read it back with maxSeen().
// ===========================================================================
class UidAllocator {
public:
    // Fold a tile's parsed MDDF + MODF lists into the running maximum.
    void scanTile(const std::vector<DoodadDef>& doodads,
                  const std::vector<WmoDef>&    wmos);
    void scanTile(const Adt& adt) { scanTile(adt.doodads, adt.wmos); }

    // Mint a fresh id (strictly greater than everything scanned so far).
    // From an empty scan the first id handed out is 1.
    uint32_t next()          { return ++max_; }
    uint32_t maxSeen() const { return max_; }

private:
    uint32_t max_ = 0;
};

// Detect uniqueId collisions across the supplied lists (doodads and WMOs
// share one id space) and reassign a fresh id to every duplicate after the
// first occurrence. Folds both lists into `uids` first so replacement ids can
// never collide with an id still present. Returns how many were reassigned.
int fixDuplicateUids(std::vector<DoodadDef>& doodads,
                     std::vector<WmoDef>&    wmos,
                     UidAllocator&           uids);

// ===========================================================================
// Typed clipboard. copySelection() stores each entry relative to a pivot
// (component-wise median of the selection's world positions by default) so a
// paste rebuilds the group's shape around any anchor point.
// ===========================================================================
struct PlacementClip {
    bool        isWmo = false;
    std::string modelPath;         // archived model path (from modelName)
    uint32_t    nameIndex = 0;     // MMID/MWID index carried through copy->paste
    Vec3        relPos;            // world-space offset from the copy pivot
    Vec3        rotDeg;            // native rot[0..2], Euler degrees
    float       scale = 1.0f;      // normalized (1.0 == unit); always 1 for WMOs
    uint16_t    doodadSet = 0;     // WMO only
    uint16_t    nameSet   = 0;     // WMO only
};

// Component-wise median of the world-space positions of all entries (upper
// median for even counts). Returns (0,0,0) for an empty selection.
Vec3 medianPosition(const std::vector<DoodadDef>& doodads,
                    const std::vector<WmoDef>&    wmos);

// Snapshot a selection into clips, positions stored relative to `pivotWorld`.
std::vector<PlacementClip> copySelection(const std::vector<DoodadDef>& doodads,
                                         const std::vector<WmoDef>&    wmos,
                                         const Vec3&                   pivotWorld);
// Convenience: pivot defaults to medianPosition(doodads, wmos).
std::vector<PlacementClip> copySelection(const std::vector<DoodadDef>& doodads,
                                         const std::vector<WmoDef>&    wmos);

// Where a paste lands. OnSelection and AtPoint place the group rigidly at the
// anchor (the caller picks the anchor: the selected object's position vs. an
// arbitrary cursor point); OnTerrain additionally re-homes each pasted
// record's Z to HeightFn at its own XY, draping the group over the ground.
enum class PasteMode { OnTerrain, OnSelection, AtPoint };

// Per-paste jitter settings (deterministic: same seed + same clip list =>
// byte-identical output). Rotation/tilt are ADDED to each clip's stored
// rotation, so a copied group keeps its relative orientations. M2 scale
// jitter multiplies the clip scale and quantizes through the uint16
// (1024 == 1.0) MDDF encoding; WMOs cannot scale, so their scale is never
// touched (tilt is still allowed -- it is rotation only).
struct PasteParams {
    bool     randomRotation = false;   // uniform yaw added to rot[1]
    float    minRotDeg = -180.0f, maxRotDeg = 180.0f;
    bool     randomTilt = false;       // pitch AND roll, drawn independently
    float    minTiltDeg = -5.0f,  maxTiltDeg = 5.0f;
    bool     randomScale = false;      // M2 multiplier; quantized to /1024 steps
    float    minScale = 0.9f,     maxScale = 1.1f;
    bool     rotateToGround = false;   // orient local +Z to NormalFn, keep yaw
    uint32_t seed = 0;
};

// A paste produces both kinds of record, in clip order within each kind.
struct PasteResult {
    std::vector<DoodadDef> doodads;
    std::vector<WmoDef>    wmos;
};

// Instantiate the clipboard at `anchorWorld` (world space). Every new record
// gets a fresh uniqueId from `uids`. `height` is required only for
// PasteMode::OnTerrain, `normal` only for PasteParams::rotateToGround; either
// may be null otherwise (a null callback degrades to "no snap"/"no align").
// Pasted WMO extents are left zero -- recompute them from the model's bounds
// at save time. An empty clipboard yields an empty result.
PasteResult paste(const std::vector<PlacementClip>& clips,
                  PasteMode                         mode,
                  const Vec3&                       anchorWorld,
                  const PasteParams&                params,
                  UidAllocator&                     uids,
                  const HeightFn&                   height,
                  const NormalFn&                   normal);

// ===========================================================================
// Snap-to-ground: re-home each record's height to HeightFn at its own XY
// (only the stored up-axis component changes; the horizontal position bytes
// are untouched). With alignToNormal, additionally rebuild the rotation so
// the model's local +Z maps onto NormalFn's surface normal while keeping the
// yaw. This is also the "models follow terrain" primitive a sculpt pass calls
// after height edits.
// ===========================================================================
void snapToGround(std::vector<DoodadDef>& doodads, const HeightFn& height,
                  const NormalFn& normal, bool alignToNormal);
void snapToGround(std::vector<WmoDef>& wmos, const HeightFn& height,
                  const NormalFn& normal, bool alignToNormal);

// ===========================================================================
// Replace-preserving-transform (bulk re-theming): swap the model reference of
// the selected records while keeping position/rotation/scale (and uniqueId)
// bit-identical. `newNameIndex` is the MMID/MWID index of the new model (0 if
// the caller rebuilds name tables at save time). Out-of-range selection
// indices are ignored. Returns how many records were changed.
// ===========================================================================
int replacePreservingTransform(std::vector<DoodadDef>&    doodads,
                               const std::vector<size_t>& selectionIdx,
                               const std::string&         newModelPath,
                               uint32_t                   newNameIndex = 0);
int replacePreservingTransform(std::vector<WmoDef>&       wmos,
                               const std::vector<size_t>& selectionIdx,
                               const std::string&         newModelPath,
                               uint32_t                   newNameIndex = 0);

// ===========================================================================
// CSV sidecar export: header + one ';'-separated row per instance,
//   ModelFile;PosX;PosY;PosZ;RotX;RotY;RotZ;Scale;Kind
// Positions/rotations are the NATIVE stored MDDF/MODF values (rotations in
// degrees); Scale is normalized /1024 for M2 and a literal 1 for WMO; Kind is
// "m2" or "wmo". Fields containing the separator (or quotes/newlines) are
// double-quoted with embedded quotes doubled. `nameResolver` is consulted for
// entries whose modelName is empty (maps isWmo + MMID/MWID index -> path);
// may be null.
// ===========================================================================
using NameResolver = std::function<std::string(bool isWmo, uint32_t nameIndex)>;

std::string exportPlacementsCsv(const std::vector<DoodadDef>& doodads,
                                const std::vector<WmoDef>&    wmos,
                                const NameResolver&           nameResolver = nullptr);

} // namespace wf
