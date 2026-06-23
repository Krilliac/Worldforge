#pragma once
// ---------------------------------------------------------------------------
// SoundEntries.dbc typed view (vanilla 1.12.1 / build 5875). Server PLAY_SOUND /
// PLAY_MUSIC (and many client triggers) reference a sound by its SoundEntries
// id; this resolves that id to the actual file(s) under the sound MPQ so the
// engine can extract and play them. The full archived path of a variant is
// `DirectoryBase \ File[i]`.
//
// Field layout (29 uint32 columns, every DBC field is 4 bytes; string fields
// hold a byte offset into the string block) cross-checked vs mangos-zero
// DBCStructure.h SoundEntriesEntry + wowdev.wiki. As with the other DBC views,
// confirm against a real SoundEntries.dbc before trusting variant ordering.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "wow_files.hpp"   // Dbc

namespace wf {

struct SoundEntry {
    uint32_t                   id   = 0;   // field 0
    uint32_t                   type = 0;   // field 1 (SoundType)
    std::string                name;       // field 2  (internal name)
    std::array<std::string,10> files;      // fields 3..12  (File[10])
    std::string                directory;  // field 23 (DirectoryBase)
    float                      volume = 1; // field 24
    uint32_t                   flags  = 0; // field 25

    // Archived path of variant `i` (DirectoryBase \ File[i]); "" if that slot
    // is empty. Uses backslashes to match the MPQ/readFile convention.
    std::string path(size_t i) const {
        if (i >= files.size() || files[i].empty()) return {};
        if (directory.empty()) return files[i];
        return directory + "\\" + files[i];
    }
    // First non-empty variant path (""), if any.
    std::string firstPath() const {
        for (size_t i = 0; i < files.size(); ++i) {
            std::string p = path(i);
            if (!p.empty()) return p;
        }
        return {};
    }
};

// Typed accessor for record `rec` in [0, dbc.recordCount()).
SoundEntry soundEntry(const Dbc& dbc, uint32_t rec);

// Convenience: read every record into an id->entry map.
std::vector<SoundEntry> soundEntries(const Dbc& dbc);

} // namespace wf
