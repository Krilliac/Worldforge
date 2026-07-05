#include "test.hpp"
#include "dbc_defs.hpp"
#include "wow_files.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace wf;

namespace {
// A DBC float field is a uint32 bit pattern.
uint32_t f2u(float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u; }
}  // namespace

void test_dbc_schema() {
    std::printf("[dbc_schema]\n");

    // --- schema registry + column metadata ----------------------------------
    {
        // findSchema resolves known tables (case-insensitively), misses cleanly.
        CHECK(findSchema("AreaTable") != nullptr);
        CHECK(findSchema("areatable") == findSchema("AreaTable"));
        CHECK(findSchema("NoSuchTable") == nullptr);
        CHECK(findSchema("") == nullptr);

        // Every registered schema round-trips through findSchema, leads with
        // exactly one id column, and spans at least one field.
        for (size_t i = 0; i < schemaCount(); ++i) {
            const TableSchema& s = schemaAt(i);
            CHECK(findSchema(s.dbcName) == &s);
            CHECK(s.colCount > 0 && s.cols[0].isId);
            int idCols = 0;
            for (size_t c = 0; c < s.colCount; ++c) idCols += s.cols[c].isId ? 1 : 0;
            CHECK(idCols == 1);
            CHECK(schemaFieldCount(s) >= 1);
        }

        // Full-record schemas: field counts match the 1.12.1.5875 record
        // strides the typed structs were verified against (stride / 4).
        struct { const char* table; size_t fields; } expected[] = {
            {"AreaTable", 28},        {"LiquidType", 4},
            {"ItemDisplayInfo", 23},  {"AnimationData", 7},
            {"GroundEffectTexture", 11},
            {"Light", 12},            {"LightParams", 8},
            {"LightIntBand", 34},     {"LightFloatBand", 34},
        };
        for (const auto& x : expected) {
            const TableSchema* s = findSchema(x.table);
            CHECK(s != nullptr);
            CHECK(schemaFieldCount(*s) == x.fields);
        }

        // Column field offsets agree with the typed readers' hardcoded indices.
        const TableSchema* area = findSchema("AreaTable");
        CHECK(schemaFieldOffsetOf(*area, "ExplorationLevel")   == 10);
        CHECK(schemaFieldOffsetOf(*area, "AreaName_lang")      == 11);
        CHECK(schemaFieldOffsetOf(*area, "FactionGroupMask")   == 20);
        CHECK(schemaFieldOffsetOf(*area, "LiquidTypeID")       == 21);
        CHECK(schemaFieldOffsetOf(*area, "MinElevation")       == 25);
        CHECK(schemaFieldOffsetOf(*area, "Ambient_multiplier") == 26);
        CHECK(schemaFieldOffsetOf(*area, "Lightid")            == 27);
        CHECK(schemaFieldOffsetOf(*area, "NoSuchColumn")       == -1);

        // FK click-through metadata: CreatureDisplayInfo.ModelID names
        // CreatureModelData/ID; AreaTable liquid slots name LiquidType/ID.
        const TableSchema* cdi = findSchema("CreatureDisplayInfo");
        CHECK(cdi != nullptr);
        const ColumnDef* modelCol = nullptr;
        for (size_t c = 0; c < cdi->colCount; ++c)
            if (std::string_view(cdi->cols[c].name) == "ModelID") modelCol = &cdi->cols[c];
        CHECK(modelCol != nullptr);
        CHECK(modelCol->fkTable  && std::string_view(modelCol->fkTable)  == "CreatureModelData");
        CHECK(modelCol->fkColumn && std::string_view(modelCol->fkColumn) == "ID");

        const ColumnDef* liq = nullptr;
        for (size_t c = 0; c < area->colCount; ++c)
            if (std::string_view(area->cols[c].name) == "LiquidTypeID") liq = &area->cols[c];
        CHECK(liq != nullptr && liq->arrayLen == 4);
        CHECK(liq->fkTable && std::string_view(liq->fkTable) == "LiquidType");

        // VERIFY-FLAGGED readers surface as verified == false in the schema.
        const ColumnDef* scale = nullptr;
        for (size_t c = 0; c < cdi->colCount; ++c)
            if (std::string_view(cdi->cols[c].name) == "CreatureModelScale") scale = &cdi->cols[c];
        CHECK(scale != nullptr && !scale->verified);
    }

    // --- AreaTable extended parse (full 28-field 5875 row) -------------------
    {
        DbcBuilder b(28);
        std::vector<uint32_t> row(28, 0);
        row[0]  = 12;                       // id (Elwynn Forest)
        row[1]  = 0;                        // ContinentID
        row[2]  = 0;                        // ParentAreaNum
        row[10] = 10;                       // ExplorationLevel
        row[11] = b.addString("Elwynn Forest");
        row[20] = 2;                        // FactionGroupMask (Alliance)
        row[21] = 1; row[22] = 2;           // LiquidTypeID[0..1] water/ocean
        row[23] = 3; row[24] = 4;           // LiquidTypeID[2..3] magma/slime
        row[25] = f2u(-500.0f);             // MinElevation
        row[26] = f2u(0.75f);               // Ambient_multiplier
        row[27] = 9;                        // Lightid
        b.addRecord(row);
        Dbc dbc = Dbc::parse(b.build());

        AreaEntry e = areaEntry(dbc, 0);
        CHECK(e.id == 12);
        CHECK(e.name == "Elwynn Forest");
        CHECK(e.explorationLevel == 10);
        CHECK(e.factionGroupMask == 2);
        CHECK(e.liquidTypeId[0] == 1 && e.liquidTypeId[1] == 2);
        CHECK(e.liquidTypeId[2] == 3 && e.liquidTypeId[3] == 4);
        CHECK_APPROX(e.minElevation, -500.0f);
        CHECK_APPROX(e.ambientMultiplier, 0.75f);
        CHECK(e.lightId == 9);
    }

    // --- AreaTable short row (older build) must not crash --------------------
    {
        DbcBuilder b(12);   // pre-5875-style row: stops right after the name
        std::vector<uint32_t> row(12, 0);
        row[0] = 33; row[11] = b.addString("Short Zone");
        b.addRecord(row);
        Dbc dbc = Dbc::parse(b.build());

        AreaEntry e = areaEntry(dbc, 0);    // fields 20..27 are absent -> zeros
        CHECK(e.id == 33);
        CHECK(e.name == "Short Zone");
        CHECK(e.factionGroupMask == 0);
        CHECK(e.liquidTypeId[0] == 0 && e.liquidTypeId[3] == 0);
        CHECK(e.minElevation == 0.0f && e.ambientMultiplier == 0.0f);
        CHECK(e.lightId == 0);
    }

    // --- LiquidType: name resolves; raw offset kept; spellID round-trips -----
    {
        DbcBuilder b(4);
        uint32_t offWater = b.addString("Water");
        uint32_t offSlime = b.addString("Slime");
        b.addRecord({1, offWater, 3, 0});        // Water, vanilla type 3
        b.addRecord({4, offSlime, 2, 28865});    // Slime, Naxx aura spell
        Dbc dbc = Dbc::parse(b.build());

        LiquidTypeEntry w = liquidTypeEntry(dbc, 0);
        CHECK(w.id == 1);
        CHECK(w.name == "Water");
        CHECK(w.liquidId == offWater);   // the raw string-block byte offset
        CHECK(w.type == 3 && w.spellId == 0);

        LiquidTypeEntry s = liquidTypeEntry(dbc, 1);
        CHECK(s.name == "Slime");
        CHECK(s.type == 2);
        CHECK(s.spellId == 28865);       // nonzero spell survives the round-trip
    }

    // --- vanilla <-> wrath liquid-type translation ---------------------------
    {
        // Forward: vanilla enum -> wrath enum.
        CHECK(liquidTypeVanillaToWrath(0) == 2);              // magma
        CHECK(liquidTypeVanillaToWrath(2) == 3);              // slime
        CHECK(liquidTypeVanillaToWrath(3) == 0);              // water
        CHECK(liquidTypeVanillaToWrath(3, /*ocean=*/true) == 1);
        // Backward: wrath enum -> vanilla enum (ocean collapses onto 3).
        CHECK(liquidTypeWrathToVanilla(0) == 3);
        CHECK(liquidTypeWrathToVanilla(1) == 3);
        CHECK(liquidTypeWrathToVanilla(2) == 0);
        CHECK(liquidTypeWrathToVanilla(3) == 2);
        // Involution on {water, ocean, magma, slime}: round-tripping each wrath
        // value through vanilla (with the ocean disambiguator for wrath 1)
        // returns the original value.
        for (uint32_t wrath = 0; wrath < 4; ++wrath)
            CHECK(liquidTypeVanillaToWrath(liquidTypeWrathToVanilla(wrath), wrath == 1) == wrath);
        // And every representable vanilla value survives the reverse trip.
        for (uint32_t vanilla : {0u, 2u, 3u})
            CHECK(liquidTypeWrathToVanilla(liquidTypeVanillaToWrath(vanilla)) == vanilla);
    }

    // --- Light family parse ---------------------------------------------------
    {
        // Light.dbc: 12 fields -- id, map, xyz, falloffs, 5 param refs.
        DbcBuilder b(12);
        b.addRecord({7, 1, f2u(100.0f), f2u(200.0f), f2u(50.0f),
                     f2u(300.0f), f2u(600.0f), 21, 22, 23, 24, 25});
        Dbc dbc = Dbc::parse(b.build());
        LightEntry e = lightEntry(dbc, 0);
        CHECK(e.id == 7 && e.mapId == 1);
        CHECK_APPROX(e.falloffStart, 300.0f);
        CHECK_APPROX(e.falloffEnd, 600.0f);
        CHECK(e.lightParams[0] == 21 && e.lightParams[4] == 25);
    }
    {
        // LightParams.dbc: 8 fields.
        DbcBuilder b(8);
        b.addRecord({5, 1, 3, f2u(0.5f), f2u(0.6f), f2u(0.9f), f2u(0.55f), f2u(0.85f)});
        Dbc dbc = Dbc::parse(b.build());
        LightParamsRecord p = lightParamsRecord(dbc, 0);
        CHECK(p.id == 5 && p.highlightSky == 1 && p.skyboxId == 3);
        CHECK_APPROX(p.glow, 0.5f);
        CHECK_APPROX(p.waterShallowAlpha, 0.6f);
        CHECK_APPROX(p.oceanDeepAlpha, 0.85f);
    }
    {
        // LightIntBand.dbc: 34 fields; a hostile num (99) clamps to 16.
        DbcBuilder b(34);
        std::vector<uint32_t> row(34, 0);
        row[0] = 73;                        // id (= params 5, band idx 0: 5*18-17)
        row[1] = 99;                        // absurd key count -> clamps
        row[2] = 0; row[3] = 1440;          // times
        row[18] = 0x00102030; row[19] = 0x00405060;   // colours
        b.addRecord(row);
        Dbc dbc = Dbc::parse(b.build());
        LightIntBandEntry band = lightIntBandEntry(dbc, 0);
        CHECK(band.id == 73);
        CHECK(band.num == 16);              // clamped, no overrun
        CHECK(band.times[1] == 1440 && band.colors[1] == 0x00405060);
    }
    {
        // LightSkybox.dbc: path resolves; vanilla-width rows read flags as 0.
        DbcBuilder b(3);
        uint32_t off = b.addString("Environments\\Stars\\HellFireSky.mdx");
        b.addRecord({2, off, 0});
        Dbc dbc = Dbc::parse(b.build());
        LightSkyboxEntry sky = lightSkyboxEntry(dbc, 0);
        CHECK(sky.id == 2);
        CHECK(sky.path == "Environments\\Stars\\HellFireSky.mdx");
        CHECK(sky.flags == 0);
    }

    // --- band-id arithmetic ----------------------------------------------------
    {
        CHECK(lightIntBandFirstId(1) == 1);      // params 1 owns int bands 1..18
        CHECK(lightIntBandFirstId(2) == 19);     // params 2 owns 19..36
        CHECK(lightFloatBandFirstId(1) == 1);    // params 1 owns float bands 1..6
        CHECK(lightFloatBandFirstId(3) == 13);   // params 3 owns 13..18
    }

    // --- sampleIntBand ----------------------------------------------------------
    {
        // Empty band: black.
        LightIntBandEntry none;
        CHECK(sampleIntBand(none, 0) == 0);
        CHECK(sampleIntBand(none, 1440) == 0);

        // Single key: constant at every time of day.
        LightIntBandEntry solo;
        solo.num = 1; solo.times[0] = 700; solo.colors[0] = 0x00ABCDEF;
        CHECK(sampleIntBand(solo, 0) == 0x00ABCDEF);
        CHECK(sampleIntBand(solo, 2879) == 0x00ABCDEF);

        // Two keys: exact on-key values and an exact midpoint lerp per channel.
        LightIntBandEntry two;
        two.num = 2;
        two.times[0] = 600;  two.colors[0] = 0x00204060;
        two.times[1] = 1800; two.colors[1] = 0x0060A0E0;
        CHECK(sampleIntBand(two, 600)  == 0x00204060);   // exactly on key 0
        CHECK(sampleIntBand(two, 1800) == 0x0060A0E0);   // exactly on key 1
        CHECK(sampleIntBand(two, 1200) == 0x004070A0);   // midpoint, per channel

        // Wrap across midnight: past the last key it lerps back to the first.
        // Keys at 720 and 2160; the wrapped segment spans 2160 -> 720 (1440
        // ticks), so midnight (t = 0) is its midpoint.
        LightIntBandEntry wrap;
        wrap.num = 2;
        wrap.times[0] = 720;  wrap.colors[0] = 0x00000040;
        wrap.times[1] = 2160; wrap.colors[1] = 0x000000C0;
        CHECK(sampleIntBand(wrap, 0) == 0x00000080);     // midnight midpoint
        CHECK(sampleIntBand(wrap, 2160) == 0x000000C0);  // on the last key
        CHECK(sampleIntBand(wrap, 2880) == sampleIntBand(wrap, 0));  // time wraps
    }

    // --- sampleFloatBand ---------------------------------------------------------
    {
        LightFloatBandEntry none;
        CHECK(sampleFloatBand(none, 1440) == 0.0f);

        LightFloatBandEntry solo;
        solo.num = 1; solo.times[0] = 100; solo.values[0] = 12.5f;
        CHECK_APPROX(sampleFloatBand(solo, 2000), 12.5f);

        // Float band 0 is fogEnd, stored in 36-yard units: a sampled value of
        // 15.0 means 15 * 36 = 540 yd of fog draw distance (lighting.hpp's
        // kFogDistScale applies the conversion; this band stays in band units).
        LightFloatBandEntry fog;
        fog.num = 2;
        fog.times[0] = 0;    fog.values[0] = 10.0f;
        fog.times[1] = 1440; fog.values[1] = 20.0f;
        CHECK_APPROX(sampleFloatBand(fog, 0), 10.0f);      // on key 0
        CHECK_APPROX(sampleFloatBand(fog, 720), 15.0f);    // exact midpoint
        CHECK_APPROX(sampleFloatBand(fog, 1440), 20.0f);   // on key 1
        // Wrapped segment 1440 -> 0 spans 1440 ticks; t = 2160 is its midpoint.
        CHECK_APPROX(sampleFloatBand(fog, 2160), 15.0f);
    }

    // --- lightWeight -----------------------------------------------------------
    {
        CHECK(lightWeight(0.0f, 300.0f, 600.0f) == 1.0f);      // at the centre
        CHECK(lightWeight(300.0f, 300.0f, 600.0f) == 1.0f);    // on the inner edge
        CHECK_APPROX(lightWeight(450.0f, 300.0f, 600.0f), 0.5f);  // linear middle
        CHECK(lightWeight(600.0f, 300.0f, 600.0f) == 0.0f);    // on the outer edge
        CHECK(lightWeight(9999.0f, 300.0f, 600.0f) == 0.0f);   // far outside
        // Degenerate radii (start == end) act as a hard step, no divide-by-zero.
        CHECK(lightWeight(100.0f, 250.0f, 250.0f) == 1.0f);
        CHECK(lightWeight(400.0f, 250.0f, 250.0f) == 0.0f);
    }
}
