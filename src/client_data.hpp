#pragma once
// ---------------------------------------------------------------------------
// client_data: drop WorldForge next to WoW.exe and have it find + mount the
// real client data the way the 1.12.1 client does.
//
// A retail vanilla install looks like:
//   WoW.exe
//   Data/
//     base.MPQ dbc.MPQ interface.MPQ misc.MPQ model.MPQ sound.MPQ
//     speech.MPQ terrain.MPQ texture.MPQ wmo.MPQ patch.MPQ patch-2.MPQ
//     enUS/ (locale-enUS.MPQ speech-enUS.MPQ patch-enUS.MPQ patch-enUS-2.MPQ)
//
// The base archives mount first (low priority) and patches mount last so they
// override -- exactly the client's search order. Locale archives append after,
// patched per the detected/`--locale` locale.
//
// All path generation is pure (no filesystem), so the chain ordering is unit-
// tested headlessly; mounting + detection are thin filesystem wrappers on top.
// ---------------------------------------------------------------------------
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "mpq.hpp"

namespace wf {

// The canonical vanilla 1.12.1 archive priority, LOW priority first; the two
// patch archives come last so they win. (Just the file names, not paths.)
std::vector<std::string> wowBaseArchiveNames();

// Locale archive name templates ({loc} -> locale), appended after the base set.
std::vector<std::string> wowLocaleArchiveTemplates();

// Build the full ordered chain of archive paths (low priority first) for a Data
// directory + locale. Pure: it does NOT check existence, so it is the canonical
// order regardless of which archives a given install actually ships.
std::vector<std::string> wowArchiveChain(const std::filesystem::path& dataDir,
                                         const std::string& locale);

// True if `dir` looks like a WoW Data directory (has at least one base archive).
bool looksLikeDataDir(const std::filesystem::path& dir);

// Locate a WoW Data directory starting from `start` (an exe dir or cwd):
//   - `start` itself if it IS a Data dir,
//   - `start`/Data,
//   - then walk up a few parents trying each + their /Data.
// Returns an empty path if none found.
std::filesystem::path findDataDir(const std::filesystem::path& start);

// Detect the locale by finding the first locale subdir (e.g. enUS, deDE) under
// `dataDir` that contains a locale-*.MPQ. Returns "" if none found.
std::string detectLocale(const std::filesystem::path& dataDir);

// Mount every archive in wowArchiveChain that exists on disk, in order. Returns
// the number opened. `onMount(path, ok)` is an optional progress callback.
size_t mountWowClient(MpqManager& mpq, const std::filesystem::path& dataDir,
                      const std::string& locale,
                      const std::function<void(const std::string&, bool)>& onMount = {});

} // namespace wf
