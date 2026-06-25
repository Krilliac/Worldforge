#include "dbc_index.hpp"

#include <unordered_set>

namespace wf {

MapIndex MapIndex::build(const Dbc& dbc) {
    MapIndex idx;
    uint32_t n = dbc.recordCount();
    idx.byId_.reserve(n);
    for (uint32_t r = 0; r < n; ++r) {
        MapEntry e = mapEntry(dbc, r);
        idx.byId_.emplace(e.id, std::move(e));   // first record wins on dup id
    }
    return idx;
}

const MapEntry* MapIndex::find(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &it->second;
}

AreaIndex AreaIndex::build(const Dbc& dbc) {
    AreaIndex idx;
    uint32_t n = dbc.recordCount();
    idx.byId_.reserve(n);
    for (uint32_t r = 0; r < n; ++r) {
        AreaEntry e = areaEntry(dbc, r);
        idx.byId_.emplace(e.id, std::move(e));
    }
    return idx;
}

const AreaEntry* AreaIndex::find(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &it->second;
}

std::string AreaIndex::fullName(uint32_t id, const char* sep) const {
    std::string out;
    std::unordered_set<uint32_t> seen;   // cycle guard
    uint32_t cur = id;
    while (true) {
        if (!seen.insert(cur).second) break;     // cycle: stop
        const AreaEntry* e = find(cur);
        if (!e) break;                           // missing parent / absent id
        if (!out.empty()) out += sep;
        out += e->name;
        if (e->parentAreaId == 0) break;         // reached the root
        cur = e->parentAreaId;
    }
    return out;
}

LiquidTypeIndex LiquidTypeIndex::build(const Dbc& dbc) {
    LiquidTypeIndex idx;
    uint32_t n = dbc.recordCount();
    idx.byId_.reserve(n);
    for (uint32_t r = 0; r < n; ++r) {
        LiquidTypeEntry e = liquidTypeEntry(dbc, r);
        idx.byId_.emplace(e.id, std::move(e));
    }
    return idx;
}

const LiquidTypeEntry* LiquidTypeIndex::find(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &it->second;
}

} // namespace wf
