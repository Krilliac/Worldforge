#include "dbc_defs.hpp"

#include <cstring>

namespace wf {

namespace {
// A DBC float field is just a uint32 reinterpreted.
float fieldF32(const Dbc& dbc, uint32_t rec, uint32_t field) {
    uint32_t bits = dbc.getU32(rec, field);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
} // namespace

MapEntry mapEntry(const Dbc& dbc, uint32_t rec) {
    MapEntry e;
    e.id           = dbc.getU32(rec, 0);
    e.directory    = dbc.getString(rec, 1);
    e.instanceType = dbc.getU32(rec, 2);
    e.name         = dbc.getString(rec, 4);   // enUS
    return e;
}

AreaEntry areaEntry(const Dbc& dbc, uint32_t rec) {
    AreaEntry e;
    e.id               = dbc.getU32(rec, 0);
    e.mapId            = dbc.getU32(rec, 1);
    e.parentAreaId     = dbc.getU32(rec, 2);
    e.areaBit          = dbc.getU32(rec, 3);
    e.flags            = dbc.getU32(rec, 4);
    e.explorationLevel = static_cast<int32_t>(dbc.getU32(rec, 10));
    e.name             = dbc.getString(rec, 11);  // enUS
    return e;
}

LiquidTypeEntry liquidTypeEntry(const Dbc& dbc, uint32_t rec) {
    LiquidTypeEntry e;
    e.id       = dbc.getU32(rec, 0);
    e.liquidId = dbc.getU32(rec, 1);
    e.type     = dbc.getU32(rec, 2);
    e.spellId  = dbc.getU32(rec, 3);
    return e;
}

LightEntry lightEntry(const Dbc& dbc, uint32_t rec) {
    LightEntry e;
    e.id           = dbc.getU32(rec, 0);
    e.mapId        = dbc.getU32(rec, 1);
    e.x            = fieldF32(dbc, rec, 2);
    e.y            = fieldF32(dbc, rec, 3);
    e.z            = fieldF32(dbc, rec, 4);
    e.falloffStart = fieldF32(dbc, rec, 5);
    e.falloffEnd   = fieldF32(dbc, rec, 6);
    for (uint32_t i = 0; i < 8; ++i) e.lightParams[i] = dbc.getU32(rec, 7 + i);
    return e;
}

} // namespace wf
