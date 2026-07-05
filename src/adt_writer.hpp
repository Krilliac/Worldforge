#pragma once
// ---------------------------------------------------------------------------
// ADT save pipeline: full re-serialization with growth, dirty-tile tracking
// and the autosave policy.
//
// The writeAdt* functions in terrain.hpp are surgical SAME-SIZE patchers --
// they poke edited bytes over the original file and cannot express edits that
// change a chunk's size (painting grows MCAL, ensureLayer grows MCLY, placing
// objects grows MDDF/MODF, painting shadow onto a shadow-less chunk inserts a
// whole MCSH). writeAdtFull() is the genuinely new piece: it re-serializes an
// entire tile chunk-by-chunk from the parsed data model (wow_files.hpp Adt +
// terrain.hpp MapChunk), regenerating every offset, count and name table, so
// ANY edit round-trips. Layout is vanilla ADT v18, cross-checked against this
// repo's own parsers (parseAdt / parseChunks) -- parse(writeAdtFull(parse(x)))
// must reproduce the full parsed model, which is the acceptance bar the unit
// tests enforce. Format facts verified against wowdev.wiki ADT/v18.
//
// Saves NEVER touch the MPQs or original files: they land in a project
// overlay directory that SHADOWS the archive chain (client_data.hpp
// overlayFilePath / AssetLoader::setOverlayDir), mirroring the client's own
// loose-file resolution.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "terrain.hpp"
#include "wow_files.hpp"

namespace wf {

// Everything one tile's re-serialization needs: the parsed placements + name
// tables (Adt) and the parsed MCNK chunks. This is exactly what parseAdt /
// parseChunks yield (and what TileScene retains as sourceAdt/sourceChunks),
// so "state" is simply the live editing model handed back to the writer.
struct ParsedTileState {
    Adt                   adt;      // MTEX + MDDF/MODF placement lists
    std::vector<MapChunk> chunks;   // the tile's MCNKs (vanilla: 256)
};

// Re-serialize a whole tile to valid ADT v18 bytes:
//   MVER(18); MHDR (offsets fixed up last); MCIN (256 entries, recomputed
//   last); MTEX; MMDX/MMID and MWMO/MWID regenerated from the placement
//   lists' model names (deduplicated, order = first use -- so stale
//   mmidIndex/mwidIndex values on the input records are ignored and
//   reassigned); MDDF (36 B/entry) / MODF (64 B/entry); then one MCNK per
//   chunk with a rebuilt 128-byte header and its sub-chunks in canonical
//   order MCVT, MCNR (+ the 13 pad bytes vanilla leaves OUTSIDE the declared
//   size), MCLY, MCRF, MCAL, MCSH (when a shadow map is present), MCLQ (via
//   encodeMclq; header sizeLiquid = bytes + 8, 0 when dry), MCSE.
// Header flags are refreshed from the data (MCNK_HAS_MCSH, the MCNK_LQ_*
// bits via mcnkLiquidFlags); every other flag bit is preserved. Sub-chunk
// header offsets are relative to the MCNK data start (the layout the parser
// auto-detects); MCIN offsets are absolute from the file start.
//
// Chunks the parser refused (MCNK_HIGH_RES_HOLES, parsed to defaults) would
// re-serialize from those defaults -- don't feed post-vanilla tiles through
// this path.
std::vector<uint8_t> writeAdtFull(const Adt& adt, const std::vector<MapChunk>& chunks);
inline std::vector<uint8_t> writeAdtFull(const ParsedTileState& state) {
    return writeAdtFull(state.adt, state.chunks);
}

// ===========================================================================
// Dirty-tile tracking -- which tiles the save-changed driver must rewrite.
// ===========================================================================
struct DirtyTiles {
    std::set<std::pair<int, int>> tiles;   // (x, y) tile indices

    void mark(int x, int y) { tiles.insert({ x, y }); }
    bool contains(int x, int y) const { return tiles.count({ x, y }) != 0; }
    bool empty() const { return tiles.empty(); }
    void clear() { tiles.clear(); }

    // Every tile that references a placement uniqueId. Placements straddle
    // tile borders (each bordering ADT stores its own MDDF/MODF copy, keyed
    // by the shared uniqueId), so moving an object must dirty EVERY tile
    // carrying that id -- saving only the tile under the cursor would leave a
    // stale duplicate in the neighbour. The caller supplies the lookup (its
    // loaded-tile index, or a synthetic map in tests).
    using TileRefsFn = std::function<std::vector<std::pair<int, int>>(uint32_t uniqueId)>;
    void markForPlacement(uint32_t uniqueId, const TileRefsFn& tileRefs);
};

// ===========================================================================
// Autosave policy -- pure decision logic; the file IO lives with the save
// drivers (AssetLoader) so this stays headless-testable.
// ===========================================================================
struct AutosavePolicy {
    uint64_t intervalMs = 600000;   // autosave at most every 10 minutes
    uint64_t idleMs     = 2000;     // ... and only after >2 s without input
    int      maxBackups = 50;       // rolling backup ring size
};

// True when an autosave should fire now: something changed since the last
// save (modCount != lastModCount), the user has been idle for MORE than
// policy.idleMs (so a save never lands mid-stroke), and at least
// policy.intervalMs elapsed since lastSaveMs. All times are the same
// monotonic millisecond clock; a lastInputMs/lastSaveMs in the future
// (clock weirdness) fails the check rather than firing early.
bool shouldAutosave(const AutosavePolicy& policy, uint64_t nowMs,
                    uint64_t lastSaveMs, uint64_t lastInputMs,
                    uint64_t modCount, uint64_t lastModCount);

// Name (and rotate) the next slot of a rolling backup ring in `dir`:
// backups are "<base>.1.adt" (oldest) .. "<base>.N.adt" (newest). When the
// ring is full (N == maxBackups), the oldest is deleted and the survivors
// renumbered down so the returned name is always "<base>.<maxBackups>.adt"
// at steady state. Creates `dir` if needed; returns the path the caller
// should write the new backup to (this function does the rotation IO but
// never writes the backup itself).
std::filesystem::path nextBackupName(const std::filesystem::path& dir,
                                     const std::string& base, int maxBackups);

} // namespace wf
