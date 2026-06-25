// Locks the ClientProfile scaffolding seam (src/client_version.hpp):
//  * the vanilla 1.12.1 profile carries exactly the constants the research note
//    (docs/research/1-expansion-deltas.md) asserts, and
//  * profileFor() returns the vanilla profile for Vanilla_1_12_1 and the
//    populated TBC/WotLK/Cata profiles for those builds, and throws
//    std::runtime_error("unsupported client version") for an out-of-enum value
//    -- i.e. the seam fails loud rather than silently mis-parsing.
//
// Pure / no filesystem: this is a constants + control-flow test only.

#include "test.hpp"
#include "../src/client_version.hpp"

#include <cstring>
#include <stdexcept>

using namespace wf;

// --- compile-time locks (vanilla1121Profile is constexpr) -------------------
static_assert(static_cast<uint16_t>(ClientVersion::Vanilla_1_12_1) == 5875, "vanilla build id");
static_assert(static_cast<uint16_t>(ClientVersion::TBC_2_4_3)      == 8606, "tbc build id");
static_assert(static_cast<uint16_t>(ClientVersion::WotLK_3_3_5a)   == 12340, "wotlk build id");
static_assert(static_cast<uint16_t>(ClientVersion::Cata_4_3_4)     == 15595, "cata build id");

static_assert(vanilla1121Profile().version == ClientVersion::Vanilla_1_12_1, "");
static_assert(vanilla1121Profile().build == 5875, "");
static_assert(vanilla1121Profile().archive == ArchiveKind::Mpq, "");
static_assert(vanilla1121Profile().adtLayout == AdtLayout::Monolithic, "");
static_assert(vanilla1121Profile().defaultBigAlpha == false, "");
static_assert(vanilla1121Profile().heightTexturing == false, "");
static_assert(vanilla1121Profile().liquid == LiquidFormat::Mclq, "");
static_assert(vanilla1121Profile().m2Version == 0x100, "");
static_assert(vanilla1121Profile().m2Skins == M2SkinSource::Embedded, "");
static_assert(vanilla1121Profile().m2Anims == M2AnimSource::Embedded, "");
static_assert(vanilla1121Profile().m2TrackNested == false, "");
static_assert(vanilla1121Profile().wmoVersion == 17, "");
static_assert(vanilla1121Profile().dbc == DbcContainer::Wdbc, "");
static_assert(vanilla1121Profile().dbcLocaleStrings == false, "");

void test_client_version() {
    // --- vanilla profile field values (runtime mirror of the static_asserts) ---
    const ClientProfile v = vanilla1121Profile();
    CHECK(v.version == ClientVersion::Vanilla_1_12_1);
    CHECK(v.build == 5875u);
    CHECK(v.archive == ArchiveKind::Mpq);
    CHECK(v.adtLayout == AdtLayout::Monolithic);
    CHECK(v.defaultBigAlpha == false);
    CHECK(v.heightTexturing == false);
    CHECK(v.liquid == LiquidFormat::Mclq);
    CHECK(v.m2Version == 0x100u);
    CHECK(v.m2Skins == M2SkinSource::Embedded);
    CHECK(v.m2Anims == M2AnimSource::Embedded);
    CHECK(v.m2TrackNested == false);
    CHECK(v.wmoVersion == 17u);
    CHECK(v.dbc == DbcContainer::Wdbc);
    CHECK(v.dbcLocaleStrings == false);

    // --- profileFor(Vanilla) returns the same vanilla profile ---------------
    const ClientProfile pv = profileFor(ClientVersion::Vanilla_1_12_1);
    CHECK(pv.version == ClientVersion::Vanilla_1_12_1);
    CHECK(pv.build == v.build);
    CHECK(pv.adtLayout == v.adtLayout);
    CHECK(pv.liquid == v.liquid);
    CHECK(pv.m2Version == v.m2Version);
    CHECK(pv.dbc == v.dbc);

    // --- profileFor(TBC/WotLK/Cata) now resolve (no longer throw) ------------
    const ClientProfile tbc = profileFor(ClientVersion::TBC_2_4_3);
    CHECK(tbc.version == ClientVersion::TBC_2_4_3);
    CHECK(tbc.build == 8606u);
    CHECK(tbc.archive == ArchiveKind::Mpq);
    CHECK(tbc.liquid == LiquidFormat::Mclq);
    CHECK(tbc.m2Version == 0x104u);
    CHECK(tbc.m2Skins == M2SkinSource::Embedded);
    CHECK(tbc.m2TrackNested == false);
    CHECK(tbc.dbc == DbcContainer::Wdbc);

    const ClientProfile wotlk = profileFor(ClientVersion::WotLK_3_3_5a);
    CHECK(wotlk.version == ClientVersion::WotLK_3_3_5a);
    CHECK(wotlk.build == 12340u);
    CHECK(wotlk.liquid == LiquidFormat::Mh2o);
    CHECK(wotlk.m2Skins == M2SkinSource::ExternalSkin);
    CHECK(wotlk.m2TrackNested == true);
    CHECK(wotlk.m2Version == 0x108u);
    CHECK(wotlk.adtLayout == AdtLayout::Monolithic);
    CHECK(wotlk.dbc == DbcContainer::Wdbc);

    const ClientProfile cata = profileFor(ClientVersion::Cata_4_3_4);
    CHECK(cata.version == ClientVersion::Cata_4_3_4);
    CHECK(cata.build == 15595u);
    CHECK(cata.adtLayout == AdtLayout::SplitTexObj);
    CHECK(cata.liquid == LiquidFormat::Mh2o);
    CHECK(cata.heightTexturing == true);
    CHECK(cata.defaultBigAlpha == true);
    CHECK(cata.m2Anims == M2AnimSource::ExternalAnim);
    CHECK(cata.m2TrackNested == true);
    CHECK(cata.dbc == DbcContainer::Wdb2);
    CHECK(cata.dbcLocaleStrings == true);

    // --- an out-of-enum value also throws (default branch) -------------------
    {
        bool threw = false;
        try {
            (void)profileFor(static_cast<ClientVersion>(0));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
    }
}
