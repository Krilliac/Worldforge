// Locks the ClientProfile scaffolding seam (src/client_version.hpp):
//  * the vanilla 1.12.1 profile carries exactly the constants the research note
//    (docs/research/1-expansion-deltas.md) asserts, and
//  * profileFor() returns the vanilla profile for Vanilla_1_12_1 and throws
//    std::runtime_error("unsupported client version") for every non-vanilla
//    value -- i.e. the seam fails loud rather than silently mis-parsing.
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

    // --- profileFor(non-vanilla) throws the documented error ----------------
    const ClientVersion nonVanilla[] = {
        ClientVersion::TBC_2_4_3,
        ClientVersion::WotLK_3_3_5a,
        ClientVersion::Cata_4_3_4,
    };
    for (ClientVersion cv : nonVanilla) {
        bool threw = false;
        bool rightMsg = false;
        try {
            (void)profileFor(cv);
        } catch (const std::runtime_error& e) {
            threw = true;
            rightMsg = (std::strcmp(e.what(), "unsupported client version") == 0);
        } catch (...) {
            threw = true; // wrong exception type -> rightMsg stays false
        }
        CHECK(threw);
        CHECK(rightMsg);
    }

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
