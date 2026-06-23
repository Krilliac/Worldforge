#pragma once
// ---------------------------------------------------------------------------
// MpqManager: opens a chain of MPQ archives and reads files with the same
// override semantics the client uses -- archives added LATER (patches) win over
// archives added EARLIER (base data). Read-only; we never mutate client files.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

namespace wf {

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

    size_t archiveCount() const noexcept { return handles_.size(); }

private:
    // Highest priority last; searched back-to-front.
    std::vector<void*> handles_; // opaque StormLib HANDLEs
};

} // namespace wf
