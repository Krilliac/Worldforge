#include "test.hpp"
#include "liquid_color.hpp"
#include "terrain.hpp"

using namespace wf;

void test_liquid_color() {
    std::printf("[liquid_color]\n");

    // --- emissive flags: magma/slime glow, water/ocean do not -------------
    CHECK(liquidSurface(LiquidType::Magma).emissive);
    CHECK(liquidSurface(LiquidType::Slime).emissive);
    CHECK(!liquidSurface(LiquidType::River).emissive);
    CHECK(!liquidSurface(LiquidType::Ocean).emissive);

    // Water is glossy (specular set), lava is matte (~0).
    CHECK(liquidSurface(LiquidType::River).specular >
          liquidSurface(LiquidType::Magma).specular);

    // --- depth blend endpoints: depth 0 ~= baseTint, depth 1 ~= deepTint --
    // Use full light (ndotl = 1) so directional shading is at its brightest;
    // even then water never reaches its raw tint (ambient/diffuse < 1.0), so
    // compare emissive magma (no shading) for the exact endpoint identity.
    {
        LiquidSurface m = liquidSurface(LiquidType::Magma);
        Rgba shallow = liquidShade(LiquidType::Magma, 0.0f, 1.0f);
        Rgba deep    = liquidShade(LiquidType::Magma, 1.0f, 1.0f);
        CHECK(shallow.r == m.baseTint.r && shallow.g == m.baseTint.g &&
              shallow.b == m.baseTint.b && shallow.a == m.baseTint.a);
        CHECK(deep.r == m.deepTint.r && deep.g == m.deepTint.g &&
              deep.b == m.deepTint.b && deep.a == m.deepTint.a);
        // A mid depth lies strictly between the two reds.
        Rgba mid = liquidShade(LiquidType::Magma, 0.5f, 1.0f);
        CHECK(mid.r < m.baseTint.r && mid.r > m.deepTint.r);
        // Deep water is more opaque than shallow (alpha follows the blend).
        LiquidSurface w = liquidSurface(LiquidType::River);
        CHECK(w.deepTint.a > w.baseTint.a);
    }

    // --- directional shading: water darkens with ndotl, magma doesn't -----
    {
        Rgba lit  = liquidShade(LiquidType::River, 0.3f, 1.0f);   // full light
        Rgba dark = liquidShade(LiquidType::River, 0.3f, 0.0f);   // shadowed
        // Lower ndotl darkens every RGB channel (alpha unchanged).
        CHECK(dark.r < lit.r);
        CHECK(dark.g < lit.g);
        CHECK(dark.b < lit.b);
        CHECK(dark.a == lit.a);
        // Ambient floor: shadowed water is dimmed, not black.
        CHECK(dark.b > 0);

        // Magma ignores ndotl entirely -- identical under any light.
        Rgba mLit  = liquidShade(LiquidType::Magma, 0.3f, 1.0f);
        Rgba mDark = liquidShade(LiquidType::Magma, 0.3f, 0.0f);
        CHECK(mLit.r == mDark.r && mLit.g == mDark.g &&
              mLit.b == mDark.b && mLit.a == mDark.a);
    }

    // --- ndotl clamping: negative N.L behaves like 0 (no over-darkening) --
    {
        Rgba zero = liquidShade(LiquidType::Ocean, 0.5f, 0.0f);
        Rgba neg  = liquidShade(LiquidType::Ocean, 0.5f, -2.0f);
        CHECK(zero.r == neg.r && zero.g == neg.g && zero.b == neg.b);
    }

    // --- None yields a zero/transparent surface ---------------------------
    {
        LiquidSurface n = liquidSurface(LiquidType::None);
        CHECK(n.baseTint.a == 0 && n.deepTint.a == 0 && !n.emissive);
        Rgba c = liquidShade(LiquidType::None, 0.5f, 1.0f);
        CHECK(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0);
    }
}
