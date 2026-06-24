#include "lighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wf {

namespace {
// A DBC float field is a uint32 reinterpreted.
float bitsToFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Read a band record: layout is `id(0), num(1), time[16](2..17), value[16](18..33)`.
// We read only as many fields as the Dbc actually has, and clamp num to 16, so a
// short or oddly-shaped table can never overrun. Times/values are kept raw.
LightBand readBand(const Dbc& dbc, uint32_t rec) {
    LightBand b;
    uint32_t fc = dbc.fieldCount();
    if (fc < 2) return b;
    b.num = dbc.getU32(rec, 1);
    if (b.num > 16) b.num = 16;
    for (uint32_t i = 0; i < b.num; ++i) {
        uint32_t tf = 2 + i;          // time field
        uint32_t vf = 18 + i;         // value field
        b.time[i]  = (tf < fc) ? dbc.getU32(rec, tf) : 0;
        b.value[i] = (vf < fc) ? dbc.getU32(rec, vf) : 0;
    }
    return b;
}
}  // namespace

LightBand lightIntBand(const Dbc& dbc, uint32_t rec)   { return readBand(dbc, rec); }
LightBand lightFloatBand(const Dbc& dbc, uint32_t rec) { return readBand(dbc, rec); }

Vec3 unpackBandColor(uint32_t packed) {
    // Vanilla packs 0x00RRGGBB: blue in the low byte, red in bits 16..23.
    float b = static_cast<float>(packed & 0xFF) / 255.0f;
    float g = static_cast<float>((packed >> 8) & 0xFF) / 255.0f;
    float r = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
    return { r, g, b };
}

// Interpolate a generic band at tick t, returning two surrounding keys' values
// and the lerp factor. `lerp` is called with (valueA, valueB, f). Wraps over a
// 2880-tick day so the last->first segment crosses midnight.
namespace {
template <class Interp>
auto sampleBand(const LightBand& band, float t, Interp&& lerp,
                decltype(lerp(0u, 0u, 0.0f)) fallback)
    -> decltype(lerp(0u, 0u, 0.0f)) {
    if (band.num == 0) return fallback;
    if (band.num == 1) return lerp(band.value[0], band.value[0], 0.0f);

    // Normalise t into [0, 2880).
    t = std::fmod(t, kDayTicks);
    if (t < 0) t += kDayTicks;

    // Find the segment [time[i], time[i+1]) containing t; handle wraparound.
    for (uint32_t i = 0; i < band.num; ++i) {
        float t0 = static_cast<float>(band.time[i]);
        uint32_t j = (i + 1) % band.num;
        float t1 = static_cast<float>(band.time[j]);
        bool wrap = (j == 0);                 // last->first segment wraps midnight
        if (wrap) t1 += kDayTicks;
        float tt = t;
        if (wrap && tt < t0) tt += kDayTicks; // pull t into the wrapped window
        if (tt >= t0 && tt <= t1) {
            float span = t1 - t0;
            float f = span > 1e-6f ? (tt - t0) / span : 0.0f;
            f = std::clamp(f, 0.0f, 1.0f);
            return lerp(band.value[i], band.value[j], f);
        }
    }
    // t before the first key: clamp to the first value.
    return lerp(band.value[0], band.value[0], 0.0f);
}
}  // namespace

uint32_t colorFor(const LightBand& band, float t) {
    // Per-channel lerp of the packed colour, re-packed.
    auto lerpColor = [](uint32_t a, uint32_t b, float f) -> uint32_t {
        uint32_t out = 0;
        for (int s = 0; s < 32; s += 8) {
            float ca = static_cast<float>((a >> s) & 0xFF);
            float cb = static_cast<float>((b >> s) & 0xFF);
            uint32_t c = static_cast<uint32_t>(ca + (cb - ca) * f + 0.5f) & 0xFF;
            out |= c << s;
        }
        return out;
    };
    return sampleBand(band, t, lerpColor, 0u);
}

Vec3 colorForRgb(const LightBand& band, float t, Vec3 fallback) {
    if (band.num == 0) return fallback;
    return unpackBandColor(colorFor(band, t));
}

float floatFor(const LightBand& band, float t, float fallback) {
    if (band.num == 0) return fallback;
    auto lerpF = [](uint32_t a, uint32_t b, float f) -> float {
        float fa = bitsToFloat(a), fb = bitsToFloat(b);
        return fa + (fb - fa) * f;
    };
    return sampleBand(band, t, lerpF, fallback);
}

LightParamsEntry lightParamsEntry(const Dbc& dbc, uint32_t rec) {
    LightParamsEntry e;
    e.id = dbc.getU32(rec, 0);
    return e;
}

void LightDatabase::resolveBands(Sky& sky, uint32_t lightParamsId) const {
    // WoWMapViewer: the 18 IntBands / 6 FloatBands for a LightParams begin at
    // id = lightParamsId * count. Band ids are not guaranteed contiguous, so we
    // look up each (firstId + i) in the id->record maps and skip any that are
    // genuinely absent (leaving num==0 -> the sample falls back to a default).
    if (intTbl_) {
        uint32_t first = lightParamsId * LB_INT_COUNT;
        for (uint32_t i = 0; i < LB_INT_COUNT; ++i) {
            auto it = intBandById_.find(first + i);
            if (it != intBandById_.end()) sky.intBands[i] = lightIntBand(*intTbl_, it->second);
        }
    }
    if (floatTbl_) {
        uint32_t first = lightParamsId * kLightFloatBandCount;
        for (uint32_t i = 0; i < kLightFloatBandCount; ++i) {
            auto it = floatBandById_.find(first + i);
            if (it != floatBandById_.end()) sky.floatBands[i] = lightFloatBand(*floatTbl_, it->second);
        }
    }
}

void LightDatabase::build(const Dbc* light, const Dbc* lightParams,
                          const Dbc* lightIntBandTbl, const Dbc* lightFloatBandTbl) {
    lights_.clear();
    intBandById_.clear();
    floatBandById_.clear();
    intTbl_   = lightIntBandTbl;
    floatTbl_ = lightFloatBandTbl;
    if (!light) return;

    // Index the band tables by their id column.
    if (lightIntBandTbl) {
        for (uint32_t r = 0; r < lightIntBandTbl->recordCount(); ++r)
            intBandById_[lightIntBandTbl->getU32(r, 0)] = r;
    }
    if (lightFloatBandTbl) {
        for (uint32_t r = 0; r < lightFloatBandTbl->recordCount(); ++r)
            floatBandById_[lightFloatBandTbl->getU32(r, 0)] = r;
    }

    // Map LightParams.id is implicit (its id column is the band-table key); we do
    // not need a LightParams.id->row map because Light.dbc already stores the
    // LightParams *id* in its param[0] field. (lightParams arg kept for API
    // symmetry / future per-params alpha floors.)
    (void)lightParams;

    for (uint32_t r = 0; r < light->recordCount(); ++r) {
        LightEntry le = lightEntry(*light, r);
        Sky sky;
        sky.mapId        = le.mapId;
        sky.pos          = { le.x, le.y, le.z };
        sky.falloffStart = le.falloffStart;
        sky.falloffEnd   = le.falloffEnd;
        sky.global       = (le.x == 0.0f && le.y == 0.0f && le.z == 0.0f);
        resolveBands(sky, le.lightParams[0]);   // param[0] = clear weather
        lights_.push_back(std::move(sky));
    }
}

LightingSample LightDatabase::lightingAt(const Vec3& worldPos, uint32_t mapId,
                                         float dayTick) const {
    if (lights_.empty()) return {};   // invalid -> caller keeps its default

    // Accumulate positioned skies by distance weight; the global sky fills the
    // remaining weight (WoWMapViewer findSkyWeights). Weight: 1 inside the inner
    // radius, linearly down to 0 at the outer radius, 0 beyond.
    const Sky* global = nullptr;
    float       totalW = 0.0f;
    LightingSample acc;     // zeroed accumulators below
    Vec3 ambient{ 0, 0, 0 }, diffuse{ 0, 0, 0 }, fog{ 0, 0, 0 };
    Vec3 waterDark{ 0, 0, 0 }, waterLight{ 0, 0, 0 };
    float fogEndAcc = 0.0f, fogStartMulAcc = 0.0f;

    auto addSky = [&](const Sky& s, float w) {
        Vec3 a = colorForRgb(s.intBands[LB_GLOBAL_AMBIENT], dayTick, acc.ambient);
        Vec3 d = colorForRgb(s.intBands[LB_GLOBAL_DIFFUSE], dayTick, acc.diffuse);
        Vec3 f = colorForRgb(s.intBands[LB_FOG],           dayTick, acc.fog);
        Vec3 wd = colorForRgb(s.intBands[LB_WATER_DARK],   dayTick, acc.waterDark);
        Vec3 wl = colorForRgb(s.intBands[LB_WATER_LIGHT],  dayTick, acc.waterLight);
        ambient    += a * w;
        diffuse    += d * w;
        fog        += f * w;
        waterDark  += wd * w;
        waterLight += wl * w;
        fogEndAcc      += floatFor(s.floatBands[0], dayTick, 0.0f) * w;
        fogStartMulAcc += floatFor(s.floatBands[1], dayTick, 0.5f) * w;
        totalW += w;
    };

    for (const Sky& s : lights_) {
        if (s.mapId != mapId) continue;
        if (s.global) { global = &s; continue; }
        float dx = worldPos.x - s.pos.x;
        float dy = worldPos.y - s.pos.y;
        float dz = worldPos.z - s.pos.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        float w;
        if (dist <= s.falloffStart) {
            w = 1.0f;
        } else if (dist >= s.falloffEnd || s.falloffEnd <= s.falloffStart) {
            w = 0.0f;
        } else {
            w = 1.0f - (dist - s.falloffStart) / (s.falloffEnd - s.falloffStart);
        }
        if (w > 0.0f) addSky(s, w);
    }

    // The global sky fills the rest (weight 1 - sum, never negative).
    if (global) {
        float gw = std::max(0.0f, 1.0f - totalW);
        if (gw > 0.0f) addSky(*global, gw);
    }

    if (totalW <= 1e-6f) return {};   // matched map but nothing active -> default

    float inv = 1.0f / totalW;
    LightingSample out;
    out.valid      = true;
    out.ambient    = ambient * inv;
    out.diffuse    = diffuse * inv;
    out.fog        = fog * inv;
    out.waterDark  = waterDark * inv;
    out.waterLight = waterLight * inv;
    float fogEnd   = (fogEndAcc * inv) * kFogDistScale;
    float fogMul   = std::clamp(fogStartMulAcc * inv, 0.0f, 1.0f);
    out.fogEnd     = fogEnd;
    out.fogStart   = fogEnd * fogMul;
    return out;
}

}  // namespace wf
