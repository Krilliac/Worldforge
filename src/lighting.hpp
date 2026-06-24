#pragma once
// ---------------------------------------------------------------------------
// Zone lighting from Light.dbc (+ LightParams / LightIntBand / LightFloatBand)
// for offline software shading. Vanilla 1.12.1 (build 5875).
//
// The vanilla client picks a "sky" by world position: each Light.dbc record is a
// point with an inner (full strength) and outer (zero strength) falloff radius;
// the global record (X=Y=Z=0) covers the rest of the continent. The chosen sky's
// LightParams reference resolves to 18 colour curves in LightIntBand.dbc and 6
// float curves in LightFloatBand.dbc, each a keyframed track over a 2880-tick day
// (0 = midnight, 1440 = noon). For an offline still we sample a fixed time (noon)
// and blend the active skies by distance weight.
//
// We expose:
//   * raw band readers (lightIntBand / lightFloatBand) -- field counts are read
//     from the Dbc, never hardcoded (the 12-vs-14 Light.dbc param-count conflict
//     and the band's record shape are both data-driven);
//   * colorFor() -- the 2880-tick keyframe interpolation;
//   * LightDatabase -- builds an index over the four tables and answers
//     lightingAt(worldPos, mapId, dayTick) with a resolved LightingSample.
//
// Only LightParams ref [0] (clear weather) is used -- the right choice for a
// clear-daylight offline render. Cross-checked vs WoWMapViewer src/sky.{cpp,h}.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "dbc_defs.hpp"   // LightEntry, Dbc
#include "image.hpp"      // Rgba
#include "math.hpp"       // Vec3
#include "wow_files.hpp"  // Dbc

namespace wf {

// The 18 LightIntBand colour tracks, in record order (WoWMapViewer SkyColorNames).
// Only the ones offline shading consumes are named precisely.
enum LightIntBandIdx : uint32_t {
    LB_GLOBAL_DIFFUSE = 0,   // directional/sun diffuse colour -> scene diffuse
    LB_GLOBAL_AMBIENT = 1,   // ambient colour                 -> scene ambient
    LB_SKY_0          = 2,
    LB_FOG            = 7,    // fog / distant haze             -> scene fog colour
    LB_WATER_DARK     = 15,   // deep-water tint                -> liquid colour
    LB_WATER_LIGHT    = 16,   // shallow-water tint             -> liquid colour
    LB_SHADOW         = 17,
    LB_INT_COUNT      = 18,
};

constexpr uint32_t kLightFloatBandCount = 6;  // LightFloatBand tracks per LightParams
constexpr float    kDayTicks            = 2880.0f;  // half-minutes in a day
constexpr float    kNoonTick            = 1440.0f;  // midday sample
constexpr float    kFogDistScale        = 36.0f;    // LightFloatBand[0] -> yards

// A single keyframed band: `num` keys, each (time tick, value). For int bands the
// value is a packed colour; for float bands it is a float bit pattern. Times are
// in [0,2880). A band with num==0 is "absent" (caller supplies a default).
struct LightBand {
    uint32_t              num = 0;
    std::array<uint32_t, 16> time{};
    std::array<uint32_t, 16> value{};  // packed colour (int band) or float bits
};

// Read one LightIntBand / LightFloatBand record (0-based `rec`). The record shape
// is `id, num, time[16], value[16]` (34 fields); we still clamp `num` to 16 and
// read what the Dbc actually has, so a short/odd table never overruns.
LightBand lightIntBand(const Dbc& dbc, uint32_t rec);
LightBand lightFloatBand(const Dbc& dbc, uint32_t rec);

// Decode a packed LightIntBand colour (0x00RRGGBB; B in the low byte) to linear
// 0..1 RGB. (Vanilla packs the colour with red in the high byte of the low 24.)
Vec3 unpackBandColor(uint32_t packed);

// Sample a band at day-tick `t` (wraps over 2880). For an int band the result is
// a packed colour you pass through unpackBandColor; for a float band it is a
// float bit pattern. `colorForRgb` does the int-band sample + unpack in one step.
uint32_t colorFor(const LightBand& band, float t);
Vec3     colorForRgb(const LightBand& band, float t, Vec3 fallback);
float    floatFor(const LightBand& band, float t, float fallback);

// LightParams.dbc row we need: just its id (the key the band tables multiply by)
// and the water/ocean alpha floors (unused offline yet, kept for completeness).
struct LightParamsEntry {
    uint32_t id = 0;            // field 0
};
LightParamsEntry lightParamsEntry(const Dbc& dbc, uint32_t rec);

// Resolved lighting for one world position + time: the colours the software
// shader consumes. Colours are linear 0..1. `valid` is false when no Light.dbc
// data was available (caller keeps its own default).
struct LightingSample {
    bool valid    = false;
    Vec3 ambient{ 0.35f, 0.35f, 0.35f };  // matches the legacy fixed ambient
    Vec3 diffuse{ 0.65f, 0.65f, 0.65f };  // matches the legacy fixed diffuse
    Vec3 fog{ 0.6f, 0.7f, 0.8f };
    float fogStart = 0.0f;
    float fogEnd   = 0.0f;
    Vec3 waterDark{ 0.16f, 0.43f, 0.70f };
    Vec3 waterLight{ 0.31f, 0.55f, 0.78f };
};

// Index over the four light tables for one client. Build once from the parsed
// DBCs, then query lightingAt() per tile/camera. Tables may be empty -- the
// lookup then returns an invalid sample and callers keep the legacy fixed light.
class LightDatabase {
public:
    LightDatabase() = default;

    // Build from already-parsed DBCs. `lightParams` may be null if absent; the
    // band tables are keyed by LightParams.id (see resolve()).
    void build(const Dbc* light, const Dbc* lightParams,
               const Dbc* lightIntBandTbl, const Dbc* lightFloatBandTbl);

    bool empty() const { return lights_.empty(); }

    // Resolve lighting at a world position on map `mapId`, sampled at `dayTick`
    // (default noon). Blends positioned skies by distance, the global sky filling
    // the remainder. Returns an invalid sample if no Light data is loaded.
    LightingSample lightingAt(const Vec3& worldPos, uint32_t mapId,
                              float dayTick = kNoonTick) const;

private:
    // A resolved sky: its position/radii plus the bands its LightParams maps to.
    struct Sky {
        uint32_t mapId = 0;
        Vec3     pos{ 0, 0, 0 };
        float    falloffStart = 0.0f;
        float    falloffEnd   = 0.0f;
        bool     global       = false;  // pos == 0 -> continent default
        std::array<LightBand, LB_INT_COUNT>     intBands{};
        std::array<LightBand, kLightFloatBandCount> floatBands{};
    };

    // Fill a Sky's bands from a LightParams id, using the *18 / *6 first-id
    // formula and the id->record maps. Missing bands stay num==0.
    void resolveBands(Sky& sky, uint32_t lightParamsId) const;

    std::vector<Sky> lights_;
    // id -> record index for the band tables (band ids are NOT guaranteed
    // contiguous, so we look up by the id column, never by raw offset).
    std::unordered_map<uint32_t, uint32_t> intBandById_;
    std::unordered_map<uint32_t, uint32_t> floatBandById_;
    const Dbc* intTbl_   = nullptr;
    const Dbc* floatTbl_ = nullptr;
};

}  // namespace wf
