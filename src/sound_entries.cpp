#include "sound_entries.hpp"

#include <cstring>

namespace wf {

namespace {
// Reinterpret a uint32 field's bits as a float (DBC stores floats inline).
float asFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
} // namespace

SoundEntry soundEntry(const Dbc& dbc, uint32_t rec) {
    SoundEntry e;
    e.id   = dbc.getU32(rec, 0);
    e.type = dbc.getU32(rec, 1);
    e.name = dbc.getString(rec, 2);
    for (int i = 0; i < 10; ++i)
        e.files[i] = dbc.getString(rec, 3 + i);
    e.directory = dbc.getString(rec, 23);
    e.volume    = asFloat(dbc.getU32(rec, 24));
    e.flags     = dbc.getU32(rec, 25);
    return e;
}

std::vector<SoundEntry> soundEntries(const Dbc& dbc) {
    std::vector<SoundEntry> out;
    out.reserve(dbc.recordCount());
    for (uint32_t r = 0; r < dbc.recordCount(); ++r)
        out.push_back(soundEntry(dbc, r));
    return out;
}

} // namespace wf
