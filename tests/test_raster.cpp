#include "test.hpp"
#include "raster.hpp"

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
}
