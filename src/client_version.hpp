#pragma once
// ---------------------------------------------------------------------------
// client_version.hpp -- Multi-expansion ClientProfile scaffolding seam.
//
// WorldForge currently hard-codes vanilla 1.12.1 (client build 5875) on-disk
// format assumptions across several parsers. This header introduces a tiny
// `ClientVersion` enum plus a `ClientProfile` value-struct that *names* the
// per-version format deltas, so later waves can add TBC/WotLK/Cata by "fill in
// a profile + add a parser branch" instead of "rewrite the parser".
//
// SCOPE OF THIS WAVE (important): this is SCAFFOLDING ONLY. Vanilla 1.12.1 is
// the single implemented target and its behavior is unchanged. Nothing here is
// threaded through the existing parsers yet (that is a later wave). This file
// only lands the seam and is locked by tests/test_client_version.cpp.
//
// `profileFor()` returns the vanilla profile for ClientVersion::Vanilla_1_12_1
// and THROWS std::runtime_error("unsupported client version") for every
// non-vanilla value -- i.e. the scaffolding "fails loud" rather than silently
// mis-parsing a format it cannot yet handle.
//
// Research basis: docs/research/1-expansion-deltas.md (cross-checked against
// wowdev.wiki + real engines: TrinityCore, cmangos, wow-adt/wow-m2 crates).
//
// FIELD -> EXISTING src/ FILE MAP (where each delta will eventually branch):
//   version / build      -> (detection; client_data.cpp detectProfile, later)
//   archive              -> mpq.cpp, client_data.cpp (mountWowClient / archive list)
//   adtLayout            -> wow_files.cpp::parseAdt, terrain.cpp::parseChunks
//   defaultBigAlpha      -> terrain.cpp::decodeAlphaMap (already takes `bigAlpha`);
//                           cf. wow_files.hpp Wdt::MPHD_BIG_ALPHA (0x4) / bigAlpha()
//   heightTexturing      -> terrain.cpp (WDT MPHD 0x80 / MTXP, Cata+) -- not vanilla
//   liquid               -> terrain.cpp::parseChunks (MCLQ) / wow_files.cpp (MH2O later)
//   m2Version            -> m2.cpp::parseM2 (currently accepts MD20 / 0x100)
//   m2Skins              -> m2.cpp::parseM2 view section (embedded view 0 vanilla)
//   m2Anims              -> m2.cpp::parseM2Animation, anim.hpp
//   m2TrackNested        -> m2.cpp::parseM2Animation (RawChannel.ranges vanilla)
//   wmoVersion           -> wmo.cpp::parseWmoRoot (v17 vanilla..Cata)
//   dbc                  -> wow_files.cpp::Dbc::parse (WDBC container)
//   dbcLocaleStrings     -> wow_files.cpp::Dbc::parse (Cata localized strings)
//
// NOTE on big-alpha: the *effective* big-alpha for a given map is derived per
// map from the WDT MPHD flags (0x4 | 0x80) at parse time -- see
// wow_files.hpp Wdt::bigAlpha(). `defaultBigAlpha` here records only the
// per-version DEFAULT/allowed encoding. Real vanilla map data is effectively
// always packed 4-bit (cmangos vanilla extractor decodes no big-alpha at all),
// so the vanilla profile sets defaultBigAlpha == false.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <stdexcept>

namespace wf {

// Client build numbers double as the enum values so a profile can be keyed by
// either the human-named expansion or the raw build it came from.
enum class ClientVersion : uint16_t {
    Vanilla_1_12_1 = 5875,   // the only implemented target today
    TBC_2_4_3      = 8606,
    WotLK_3_3_5a   = 12340,
    Cata_4_3_4     = 15595,
};

// --- format-selecting categoricals (the "what changed on disk" knobs) ---

enum class ArchiveKind   { Mpq, Casc };               // Casc unused (WoD+; not a target)
enum class AdtLayout     { Monolithic, SplitTexObj }; // Cata = SplitTexObj (.adt/_tex0/_obj0)
enum class LiquidFormat  { Mclq, Mh2o };              // WotLK+ = Mh2o (tile-level)
enum class DbcContainer  { Wdbc, Wdb2 };              // Cata high-volume tables = Wdb2 (.db2)
enum class M2SkinSource  { Embedded, ExternalSkin };  // WotLK+ = ExternalSkin (Model0N.skin)
enum class M2AnimSource  { Embedded, ExternalAnim };  // Cata+  = ExternalAnim (.anim files)

// Value-struct carrying the per-version format deltas. Defaults are the vanilla
// 1.12.1 values; see vanilla1121Profile() for the canonical instance.
struct ClientProfile {
    ClientVersion version = ClientVersion::Vanilla_1_12_1;
    uint32_t      build   = 5875;

    // --- archive / mounting (client_data.cpp, mpq.cpp) ---
    ArchiveKind   archive = ArchiveKind::Mpq;

    // --- ADT (wow_files.cpp parseAdt, terrain.cpp parseChunks) ---
    AdtLayout     adtLayout = AdtLayout::Monolithic;

    // --- MCAL alpha (terrain.cpp decodeAlphaMap; cf. Wdt MPHD 0x4/0x80) ---
    // bigAlpha is *also* derived per-map from WDT MPHD (0x4|0x80); these fields
    // record the per-version DEFAULT/allowed encoding only. Vanilla = 4-bit.
    bool          defaultBigAlpha = false;   // 4096 B 8-bit per texel when true
    bool          heightTexturing = false;   // WDT MPHD 0x80 / MTXP (Cata) -- not vanilla

    // --- liquid (terrain.cpp parseChunks / wow_files.cpp) ---
    LiquidFormat  liquid = LiquidFormat::Mclq;

    // --- M2 (m2.cpp parseM2 / parseM2Animation, anim.hpp) ---
    uint32_t      m2Version    = 0x100;      // 256 == 1.0 (vanilla)
    M2SkinSource  m2Skins      = M2SkinSource::Embedded;   // embedded view 0
    M2AnimSource  m2Anims      = M2AnimSource::Embedded;   // embedded keyframes
    bool          m2TrackNested = false;     // WotLK+ nested per-anim arrays

    // --- WMO (wmo.cpp parseWmoRoot) ---
    uint16_t      wmoVersion = 17;           // v17 vanilla..Cata (v14 = pre-release alpha)

    // --- DBC (wow_files.cpp Dbc::parse) ---
    DbcContainer  dbc = DbcContainer::Wdbc;
    bool          dbcLocaleStrings = false;  // Cata: single localized string field
};

// Canonical vanilla 1.12.1 (build 5875) profile. Header-only and constexpr so
// callers may use it in constant expressions and there is zero link surface.
// Equivalent to a default-constructed ClientProfile, but spelled out explicitly
// so the locked-in vanilla constants are self-documenting at the call site.
constexpr ClientProfile vanilla1121Profile() {
    ClientProfile p{};
    p.version          = ClientVersion::Vanilla_1_12_1;
    p.build            = 5875;
    p.archive          = ArchiveKind::Mpq;
    p.adtLayout        = AdtLayout::Monolithic;
    p.defaultBigAlpha  = false;
    p.heightTexturing  = false;
    p.liquid           = LiquidFormat::Mclq;
    p.m2Version        = 0x100;
    p.m2Skins          = M2SkinSource::Embedded;
    p.m2Anims          = M2AnimSource::Embedded;
    p.m2TrackNested    = false;
    p.wmoVersion       = 17;
    p.dbc              = DbcContainer::Wdbc;
    p.dbcLocaleStrings = false;
    return p;
}

// Returns the profile for `v`. Vanilla 1.12.1 is the ONLY implemented target;
// every other version throws std::runtime_error("unsupported client version").
// This is intentional: the seam fails loud until a later wave fills in the
// TBC/WotLK/Cata profiles and the matching parser branches.
inline ClientProfile profileFor(ClientVersion v) {
    switch (v) {
        case ClientVersion::Vanilla_1_12_1:
            return vanilla1121Profile();
        case ClientVersion::TBC_2_4_3:
        case ClientVersion::WotLK_3_3_5a:
        case ClientVersion::Cata_4_3_4:
        default:
            // TODO(expansion): implement tbc/wotlk/cata profiles + parser branches.
            throw std::runtime_error("unsupported client version");
    }
}

} // namespace wf
