#pragma once
// ---------------------------------------------------------------------------
// MpqManager: opens a chain of MPQ archives and reads files with the same
// override semantics the client uses -- archives added LATER (patches) win over
// archives added EARLIER (base data). Read-only; we never mutate client files.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wf {

// Create (or overwrite) an MPQ archive at `path` and add the given in-memory
// files (archived name -> bytes; use backslash paths like
// "DBFilesClient\\Map.dbc"). Returns false on any StormLib failure. This is the
// patch-MPQ writer for custom content -- e.g. package an edited DBC into a
// patch-4.mpq the client loads, leaving the base data untouched.
bool writeMpqArchive(const std::string& path,
                     const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);

class MpqManager {
public:
    MpqManager() = default;
    ~MpqManager();

    MpqManager(const MpqManager&)            = delete;
    MpqManager& operator=(const MpqManager&) = delete;

    // Open an archive and push it to the TOP of the search chain (highest
    // priority). Returns false if the archive could not be opened.
    bool addArchive(const std::string& path);

    // True if `archivedPath` exists in any open archive.
    // Paths use backslashes, e.g. "World\\Maps\\Azeroth\\Azeroth.wdt".
    bool contains(const std::string& archivedPath) const;

    // Read an entire file into `out`. Returns false if not found anywhere.
    bool readFile(const std::string& archivedPath, std::vector<uint8_t>& out) const;

    // Enumerate archived paths matching a StormLib wildcard `mask` (`*` and `?`),
    // across the whole chain, deduplicated and sorted. The mask matches the full
    // backslash path, e.g. "*.wdt" or "World\\Maps\\*". Internal MPQ metadata
    // files ((listfile)/(attributes)/(signature)) are omitted. Powers the editor's
    // map and asset browsers; relies on each archive carrying a (listfile).
    std::vector<std::string> listFiles(const std::string& mask = "*") const;

    size_t archiveCount() const noexcept { return handles_.size(); }

private:
    // Highest priority last; searched back-to-front.
    std::vector<void*> handles_; // opaque StormLib HANDLEs
};

} // namespace wf
