// ---------------------------------------------------------------------------
// Liquid surface appearance -- see liquid_color.hpp for the rationale.
// ---------------------------------------------------------------------------
#include "liquid_color.hpp"

#include <algorithm>
#include <cstdint>

namespace wf {

namespace {

// Linear blend a -> b by t in [0,1], per channel, rounded to a byte.
uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    float v = a + (b - a) * t;
    if (v < 0.0f)   v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<uint8_t>(v + 0.5f);
}

Rgba lerpRgba(const Rgba& a, const Rgba& b, float t) {
    return Rgba{ lerp8(a.r, b.r, t), lerp8(a.g, b.g, t),
                 lerp8(a.b, b.b, t), lerp8(a.a, b.a, t) };
}

// Scale only the RGB of c by factor s (alpha untouched -- shading dims colour,
// not coverage), rounded to a byte.
Rgba scaleRgb(const Rgba& c, float s) {
    auto mul = [s](uint8_t v) {
        float r = v * s;
        if (r < 0.0f)   r = 0.0f;
        if (r > 255.0f) r = 255.0f;
        return static_cast<uint8_t>(r + 0.5f);
    };
    return Rgba{ mul(c.r), mul(c.g), mul(c.b), c.a };
}

} // namespace

LiquidSurface liquidSurface(LiquidType type) {
    switch (type) {
        // River/lake water: a bright shallow teal at the edge deepening to a
        // saturated blue, both quite translucent so terrain reads through. Water
        // is glossy (sharp sun glint) with a strong fresnel rim at grazing angle.
        // VERIFY-FLAGGED: the vanilla shallow/deep colours come from a
        // Light*Band.dbc water-colour pair; these are hand-matched, not the DBC.
        case LiquidType::River:
            return LiquidSurface{
                Rgba{  70, 140, 190, 120 },   // shallow: light teal, ~47% alpha
                Rgba{  25,  75, 140, 175 },   // deep: darker blue, more opaque
                false, 0.85f, 0.65f };

        // Ocean: like river but colder/darker and more opaque overall (deep open
        // water hides the seabed). Slightly higher fresnel than river for the
        // big flat horizon reflection.
        case LiquidType::Ocean:
            return LiquidSurface{
                Rgba{  40, 100, 160, 140 },   // shallow shelf
                Rgba{  12,  45, 100, 200 },   // deep ocean, near opaque
                false, 0.80f, 0.75f };

        // Magma: emissive -- glows regardless of light. Hot near-white/yellow in
        // thin flows cooling to deep red in the pools; fully opaque. Lava is matte
        // (no sky reflection) so specular/fresnel are ~0.
        case LiquidType::Magma:
            return LiquidSurface{
                Rgba{ 255, 180,  60, 255 },   // shallow: hot yellow-orange
                Rgba{ 200,  55,  10, 255 },   // deep: dark glowing red
                true, 0.05f, 0.0f };

        // Slime: emissive, murky toxic green. Faintly translucent at the edge,
        // opaque in the deep. Low gloss (a dull sheen), no fresnel.
        case LiquidType::Slime:
            return LiquidSurface{
                Rgba{ 150, 200,  60, 200 },   // shallow: bright sickly green
                Rgba{  55, 110,  25, 240 },   // deep: dark swamp green
                true, 0.20f, 0.0f };

        // No liquid: a fully transparent surface (nothing to draw).
        case LiquidType::None:
            break;
    }
    return LiquidSurface{ Rgba{0,0,0,0}, Rgba{0,0,0,0}, false, 0.0f, 0.0f };
}

Rgba liquidShade(LiquidType type, float depth01, float ndotl) {
    if (type == LiquidType::None) return Rgba{0, 0, 0, 0};

    LiquidSurface s = liquidSurface(type);

    float d = std::min(std::max(depth01, 0.0f), 1.0f);
    Rgba colour = lerpRgba(s.baseTint, s.deepTint, d);   // shallow -> deep

    // Emissive liquids glow uniformly: keep full brightness, ignore N.L so they
    // don't go dark on shadowed slopes (mirrors liquidEmissive()).
    if (s.emissive) return colour;

    // Directional shading: a fixed ambient floor so shadowed water never goes
    // pure black, plus the diffuse term. clamp(ndotl) to [0,1].
    float nl = std::min(std::max(ndotl, 0.0f), 1.0f);
    constexpr float ambient = 0.35f;
    float lit = ambient + (1.0f - ambient) * nl;
    return scaleRgb(colour, lit);
}

} // namespace wf
