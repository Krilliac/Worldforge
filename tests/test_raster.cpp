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
}
