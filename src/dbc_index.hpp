#pragma once
// ---------------------------------------------------------------------------
// id -> entry indices over the typed DBC decoders (dbc_defs.hpp). The per-record
// decoders give linear access; these build an id->entry hash map once from a
// parsed Dbc so callers can resolve by id in O(1). AreaIndex additionally walks
// the parentAreaId chain to produce a fully-qualified zone name
// (e.g. "Goldshire, Elwynn Forest").
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <unordered_map>

#include "dbc_defs.hpp"   // MapEntry / AreaEntry / LiquidTypeEntry decoders
#include "wow_files.hpp"   // Dbc

namespace wf {

class MapIndex {
public:
    static MapIndex build(const Dbc& dbc);
    const MapEntry* find(uint32_t id) const;   // nullptr if absent
    size_t size() const { return byId_.size(); }

private:
    std::unordered_map<uint32_t, MapEntry> byId_;
};

class AreaIndex {
public:
    static AreaIndex build(const Dbc& dbc);
    const AreaEntry* find(uint32_t id) const;  // nullptr if absent
    size_t size() const { return byId_.size(); }

    // Names joined child-first up the parentAreaId chain, e.g.
    // "Goldshire, Elwynn Forest". Stops at parentAreaId==0, a missing parent, or
    // a cycle (each id is visited at most once). "" if `id` itself is absent.
    std::string fullName(uint32_t id, const char* sep = ", ") const;

private:
    std::unordered_map<uint32_t, AreaEntry> byId_;
};

class LiquidTypeIndex {
public:
    static LiquidTypeIndex build(const Dbc& dbc);
    const LiquidTypeEntry* find(uint32_t id) const;   // nullptr if absent
    size_t size() const { return byId_.size(); }

private:
    std::unordered_map<uint32_t, LiquidTypeEntry> byId_;
};

} // namespace wf
