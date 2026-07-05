#include "test.hpp"
#include "raster.hpp"
#include "debugdraw.hpp"

using namespace wf;

void test_raster() {
    std::printf("[raster]\n");

    // ---- coverage: a centred triangle fills the middle, not the corner ----
    {
        Framebuffer fb(16, 16);
        fb.clear(Rgba{0,0,0,255});
        fillTriangleSolid(fb, {2,2,0.5f}, {14,2,0.5f}, {8,14,0.5f}, Rgba{255,0,0,255});
        CHECK(fb.color.at(8,6).r == 255);     // inside
        CHECK(fb.color.at(0,15).r == 0);      // outside
    }

    // ---- depth: nearer triangle wins regardless of draw order ----
    {
        Framebuffer fb(16, 16);
        fb.clear(Rgba{0,0,0,255});
        ScreenVert a{2,2,0}, b{14,2,0}, c{8,14,0};
        // far (red) then near (green): near should win.
        fillTriangleSolid(fb, {a.x,a.y,0.8f}, {b.x,b.y,0.8f}, {c.x,c.y,0.8f}, Rgba{255,0,0,255});
        fillTriangleSolid(fb, {a.x,a.y,0.2f}, {b.x,b.y,0.2f}, {c.x,c.y,0.2f}, Rgba{0,255,0,255});
        CHECK(fb.color.at(8,6).g == 255 && fb.color.at(8,6).r == 0);
        // far (blue) drawn last must NOT overwrite the nearer green.
        fillTriangleSolid(fb, {a.x,a.y,0.9f}, {b.x,b.y,0.9f}, {c.x,c.y,0.9f}, Rgba{0,0,255,255});
        CHECK(fb.color.at(8,6).g == 255 && fb.color.at(8,6).b == 0);
    }

    // ---- a tiny mesh projected through an MVP lands on screen ----
    {
        Framebuffer fb(32, 32);
        fb.clear(Rgba{0,0,0,255});
        Mesh m;
        m.vertices = {
            { {-1,-1,0}, {0,0,1} },
            { { 1,-1,0}, {0,0,1} },
            { { 0, 1,0}, {0,0,1} },
        };
        m.indices = {0,1,2};
        Mat4 view = Mat4::lookAt({0,0,5}, {0,0,0}, {0,1,0});
        Mat4 proj = Mat4::perspective(60.0, 1.0, 0.1, 100.0);
        rasterMesh(fb, m, proj * view, {0,0,1});
        // Something was drawn near the centre.
        bool anyLit = false;
        for (int y = 10; y < 22 && !anyLit; ++y)
            for (int x = 10; x < 22 && !anyLit; ++x)
                if (fb.color.at(x,y).g > 0) anyLit = true;
        CHECK(anyLit);
    }

    // ---- debug overlay: a line and a point marker get drawn ----
    {
        Framebuffer fb(64, 64);
        fb.clear(Rgba{0,0,0,255});
        Mat4 view = Mat4::lookAt({0,0,10}, {0,0,0}, {0,1,0});
        Mat4 proj = Mat4::perspective(60.0, 1.0, 0.1, 100.0);
        Mat4 mvp = proj * view;

        DebugDraw dd;
        dd.line({-2,0,0}, {2,0,0}, Rgba{255,0,0,255}, DebugCategory::Waypoint);
        dd.point({0,0,0}, Rgba{0,255,0,255}, DebugCategory::Marker);

        DebugDrawOptions opt; opt.depthTest = false;  // nothing behind it
        rasterDebug(fb, dd, mvp, opt);

        // A horizontal red line should light a red pixel on the centre row.
        bool red = false;
        for (int x = 0; x < 64 && !red; ++x)
            if (fb.color.at(x,32).r == 255) red = true;
        CHECK(red);
        // The green point marker sits at screen centre.
        CHECK(fb.color.at(32,32).g == 255);

        // Disabling the layer removes its primitives.
        Framebuffer fb2(64, 64);
        fb2.clear(Rgba{0,0,0,255});
        dd.setCategoryEnabled(DebugCategory::Waypoint, false);
        dd.setCategoryEnabled(DebugCategory::Marker, false);
        rasterDebug(fb2, dd, mvp, opt);
        bool anyLit = false;
        for (int y = 0; y < 64 && !anyLit; ++y)
            for (int x = 0; x < 64 && !anyLit; ++x)
                if (fb2.color.at(x,y).r || fb2.color.at(x,y).g) anyLit = true;
        CHECK(!anyLit);
    }

    // ---- rasterTexMesh alpha-blend: a translucent quad composites over bg ----
    {
        Framebuffer fb(32, 32);
        fb.clear(Rgba{0, 0, 200, 255});               // blue background
        Mat4 mvp = Mat4::perspective(60.0, 1.0, 0.1, 100.0) *
                   Mat4::lookAt({0,0,5}, {0,0,0}, {0,1,0});

        // A camera-facing quad with a half-alpha red texture.
        TexMesh q;
        q.vertices = {
            { {-2,-2,0}, {0,0,1}, {0,0} }, { {2,-2,0}, {0,0,1}, {1,0} },
            { {2,2,0},   {0,0,1}, {1,1} }, { {-2,2,0}, {0,0,1}, {0,1} },
        };
        q.indices = { 0,1,2, 0,2,3 };
        Image red(2,2);
        for (auto& p : red.pixels) p = Rgba{255, 0, 0, 128};   // 50% alpha red

        rasterTexMesh(fb, q, mvp, red, {0,0,1}, /*alphaBlend*/true);

        // Centre pixel: red over blue at ~50% -> noticeable red AND surviving blue.
        Rgba c = fb.color.at(16,16);
        CHECK(c.r > 90 && c.b > 70);                   // blend of both, not pure either
        CHECK(c.r > c.b);                              // the red is on top
    }

    // ---- rasterLiquidMesh: translucent water composites over the background --
    {
        Framebuffer fb(32, 32);
        fb.clear(Rgba{200, 80, 40, 255});             // warm (reddish) background
        Mat4 mvp = Mat4::perspective(60.0, 1.0, 0.1, 100.0) *
                   Mat4::lookAt({0,0,5}, {0,0,0}, {0,1,0});

        // A camera-facing flat quad standing in for a liquid surface (normals +Z).
        Mesh q;
        q.vertices = {
            { {-2,-2,0}, {0,0,1} }, { {2,-2,0}, {0,0,1} },
            { {2,2,0},   {0,0,1} }, { {-2,2,0}, {0,0,1} },
        };
        q.indices = { 0,1,2, 0,2,3 };

        Rgba water{ 40, 110, 180, 140 };               // translucent blue (river tint)
        rasterLiquidMesh(fb, q, mvp, water, {0,0,1}, /*emissive*/false);

        Rgba c = fb.color.at(16,16);
        CHECK(c.b > c.r);                              // blue tint dominates the warm bg
        CHECK(c.r > 0 && c.r < 200);                   // background partially survives
        // Outside the quad keeps the original background untouched.
        CHECK(fb.color.at(0,0).r == 200 && fb.color.at(0,0).b == 40);
    }

    // ---- liquid emissive vs shaded: a glowing surface ignores the light ----
    {
        Mat4 mvp = Mat4::perspective(60.0, 1.0, 0.1, 100.0) *
                   Mat4::lookAt({0,0,5}, {0,0,0}, {0,1,0});
        Mesh q;
        q.vertices = {
            { {-2,-2,0}, {0,0,1} }, { {2,-2,0}, {0,0,1} },
            { {2,2,0},   {0,0,1} }, { {-2,2,0}, {0,0,1} },
        };
        q.indices = { 0,1,2, 0,2,3 };
        Rgba lava{ 235, 110, 25, 255 };                // opaque emissive orange (magma)

        // Light pointing AWAY from the surface (-Z) would darken a shaded surface.
        Framebuffer fa(32,32); fa.clear(Rgba{0,0,0,255});
        rasterLiquidMesh(fa, q, mvp, lava, {0,0,-1}, /*emissive*/true);
        Framebuffer fb2(32,32); fb2.clear(Rgba{0,0,0,255});
        rasterLiquidMesh(fb2, q, mvp, lava, {0,0,-1}, /*emissive*/false);

        // Emissive keeps full tint; shaded (back-lit) comes out dimmer.
        CHECK(fa.color.at(16,16).r == 235);            // emissive == raw tint
        CHECK(fb2.color.at(16,16).r < fa.color.at(16,16).r);   // shaded is darker
    }

    // ---- per-vertex colour (MOCV baked light): modulates the texel ----------
    {
        Image white(1,1); white.at(0,0) = Rgba{255,255,255,255};
        // A flat-facing quad under a full-ambient light (no directional falloff),
        // so any dimming comes purely from the vertex colour.
        ShadeLight flat; flat.ambient = {1,1,1}; flat.diffuse = {0,0,0}; flat.dir = {0,0,1};
        auto quad = [](Rgba c) {
            TexMesh m;
            m.vertices = { {{-8,-8,0},{0,0,1},{0,0}, c}, {{8,-8,0},{0,0,1},{1,0}, c},
                           {{8,8,0},{0,0,1},{1,1}, c},   {{-8,8,0},{0,0,1},{0,1}, c} };
            m.indices = { 0,1,2, 0,2,3 };
            return m;
        };
        Mat4 view = Mat4::lookAt({0,0,20}, {0,0,0}, {0,1,0});
        Mat4 mvp  = Mat4::perspective(60.0, 1.0, 0.1, 100.0) * view;

        // White vertex colour: the texel passes through at full brightness.
        Framebuffer fw(32,32); fw.clear(Rgba{0,0,0,255});
        rasterTexMesh(fw, quad(Rgba{255,255,255,255}), mvp, white, flat);
        CHECK(fw.color.at(16,16).r == 255);

        // Half-grey vertex colour halves it; a red vertex colour kills g/b.
        Framebuffer fh(32,32); fh.clear(Rgba{0,0,0,255});
        rasterTexMesh(fh, quad(Rgba{128,128,128,255}), mvp, white, flat);
        CHECK(fh.color.at(16,16).r > 120 && fh.color.at(16,16).r < 135);

        Framebuffer fr(32,32); fr.clear(Rgba{0,0,0,255});
        rasterTexMesh(fr, quad(Rgba{255,0,0,255}), mvp, white, flat);
        CHECK(fr.color.at(16,16).r == 255 && fr.color.at(16,16).g == 0 && fr.color.at(16,16).b == 0);
    }
}
