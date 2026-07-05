#include "test.hpp"
#include "raster.hpp"
#include "debugdraw.hpp"
#include "m2.hpp"
#include "m2_render.hpp"
#include "scene.hpp"

#include <cmath>
#include <cstring>

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

    // ---- sequence blending: midpoint pose equals slerp of the endpoints -----
    {
        // Cross-fade weight: blendTime 0 = instant switch; otherwise
        // elapsed/blendTime, clamped to 1.
        CHECK_APPROX(m2BlendWeight(0,   0),   1.0f);
        CHECK_APPROX(m2BlendWeight(75,  150), 0.5f);
        CHECK_APPROX(m2BlendWeight(400, 150), 1.0f);

        // One bone rotating 0 -> 90deg about Z over 0..1000 ms.
        M2Animation anim;
        anim.sequences.resize(1);
        float s45 = std::sin((float)radians(45)), c45 = std::cos((float)radians(45));
        M2BoneRaw b;
        b.parent = -1;
        b.rotation.interp = 1;
        b.rotation.times  = {0, 1000};
        b.rotation.values = { Quat::identity(), Quat{0, 0, s45, c45} };
        anim.bones.push_back(b);

        // Blend the endpoint poses at w = 0.5: expected slerp(identity, 90deg,
        // 0.5) = 45deg about Z (quat components at half angle 22.5deg).
        std::vector<Mat4> mid = computePoseBlended(anim, 0, 0, 0, 1000, 0.5f);
        Mat4 expect = Quat{0, 0, std::sin((float)radians(22.5)),
                                 std::cos((float)radians(22.5))}.toMat4();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(mid[0].at(r, c), expect.at(r, c), 1e-4);

        // w clamps: 1.5 behaves as 1 (pure seqB pose).
        std::vector<Mat4> full = computePoseBlended(anim, 0, 0, 0, 1000, 1.5f);
        Mat4 endB = Quat{0, 0, s45, c45}.toMat4();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(full[0].at(r, c), endB.at(r, c), 1e-4);
    }

    // ---- UV transform: 90deg Z-quat rotation about the texture centre -------
    {
        M2Animation anim;
        float s45 = std::sin((float)radians(45)), c45 = std::cos((float)radians(45));
        M2TextureTransformRaw x;
        x.rotation.interp = 0;
        x.rotation.times  = {0};
        x.rotation.values = { Quat{0, 0, s45, c45} };   // only a Z component: the 2D case
        anim.textureTransforms.push_back(x);
        anim.textureTransformLookup.push_back(0);

        // Pivot is the texture centre: T(.5,.5) * R(90) * S * T(-.5,-.5) maps
        // (0,0) -> (1,0) exactly ((-.5,-.5) rotates to (.5,-.5), +(.5,.5)).
        Mat4 M = sampleM2TextureTransform(anim, 0, 0, 0, 0);
        Vec4 uv = M * Vec4(0, 0, 0, 1);
        CHECK_NEAR(uv.x, 1.0f, 1e-5);
        CHECK_NEAR(uv.y, 0.0f, 1e-5);

        // A lookup value of -1 means "no transform" (identity).
        anim.textureTransformLookup[0] = -1;
        Mat4 I = sampleM2TextureTransform(anim, 0, 0, 0, 0);
        Vec4 same = I * Vec4(0.25f, 0.75f, 0, 1);
        CHECK_NEAR(same.x, 0.25f, 1e-6);
        CHECK_NEAR(same.y, 0.75f, 1e-6);
    }

    // ---- UV transform scroll: a striped texture shifts by the expected texel --
    {
        // 4x1 stripe texture: R | G | B | W.
        Image stripes(4, 1);
        stripes.at(0,0) = Rgba{255,   0,   0, 255};
        stripes.at(1,0) = Rgba{  0, 255,   0, 255};
        stripes.at(2,0) = Rgba{  0,   0, 255, 255};
        stripes.at(3,0) = Rgba{255, 255, 255, 255};

        TexMesh q;
        q.vertices = {
            { {-2,-2,0}, {0,0,1}, {0,0} }, { {2,-2,0}, {0,0,1}, {1,0} },
            { {2,2,0},   {0,0,1}, {1,1} }, { {-2,2,0}, {0,0,1}, {0,1} },
        };
        q.indices = { 0,1,2, 0,2,3 };
        Mat4 mvp = Mat4::perspective(60.0, 1.0, 0.1, 100.0) *
                   Mat4::lookAt({0,0,5}, {0,0,0}, {0,1,0});
        ShadeLight flat; flat.ambient = {1,1,1}; flat.diffuse = {0,0,0}; flat.dir = {0,0,1};

        // Linear scroll track: u translation 0 -> 1 over 0..1000 ms; at 250 ms
        // the shift is +0.25 = exactly one texel of the 4-wide stripe texture.
        M2Animation anim;
        M2TextureTransformRaw x;
        x.translation.interp = 1;
        x.translation.times  = {0, 1000};
        x.translation.values = { Vec3{0,0,0}, Vec3{1,0,0} };
        anim.textureTransforms.push_back(x);
        anim.textureTransformLookup.push_back(0);

        // The screen centre samples u ~= 0.52 -> texel 2 (blue) unscrolled.
        Framebuffer f0(32, 32); f0.clear(Rgba{0,0,0,255});
        TexDrawOptions o0;
        o0.useUvTransform = true;
        o0.uvTransform    = sampleM2TextureTransform(anim, 0, 0, 0, 0);
        rasterTexMesh(f0, q, mvp, stripes, flat, o0);
        CHECK(f0.color.at(16,16).b == 255 && f0.color.at(16,16).r == 0);

        // 250 ms later: u + 0.25 -> texel 3 (white). One texel per 250 ms.
        Framebuffer f1(32, 32); f1.clear(Rgba{0,0,0,255});
        TexDrawOptions o1;
        o1.useUvTransform = true;
        o1.uvTransform    = sampleM2TextureTransform(anim, 0, 0, 250, 0);
        rasterTexMesh(f1, q, mvp, stripes, flat, o1);
        CHECK(f1.color.at(16,16).r == 255 && f1.color.at(16,16).g == 255 &&
              f1.color.at(16,16).b == 255);
    }

    // ---- translucent sort contract: submission order must not matter --------
    {
        Mat4 mvp = Mat4::perspective(60.0, 1.0, 0.1, 100.0) *
                   Mat4::lookAt({0,0,10}, {0,0,0}, {0,1,0});

        TexMesh quad;
        quad.vertices = {
            { {-3,-3,0}, {0,0,1}, {0,0} }, { {3,-3,0}, {0,0,1}, {1,0} },
            { {3,3,0},   {0,0,1}, {1,1} }, { {-3,3,0}, {0,0,1}, {0,1} },
        };
        quad.indices = { 0,1,2, 0,2,3 };
        Image red(1,1);   red.at(0,0)   = Rgba{255, 0, 0, 128};   // 50% alpha
        Image green(1,1); green.at(0,0) = Rgba{0, 255, 0, 128};

        auto framesEqual = [](const Framebuffer& a, const Framebuffer& b) {
            return a.color.pixels.size() == b.color.pixels.size() &&
                   std::memcmp(a.color.pixels.data(), b.color.pixels.data(),
                               a.color.pixels.size() * sizeof(Rgba)) == 0;
        };
        auto render = [&](const std::vector<ModelInstance>& instances) {
            Framebuffer fbo(32, 32);
            fbo.clear(Rgba{0, 0, 80, 255});
            Scene s;
            s.instances = instances;
            renderScene(fbo, s, mvp);
            return fbo;
        };

        // Case 1: same priorityPlane, different depths -- ties broken by view
        // depth, FARTHEST first.
        ModelInstance near_;                     // z=+2, 8 from the camera
        near_.mesh = &quad; near_.texture = &red;
        near_.transform = Mat4::translate({0, 0, 2});
        near_.translucent = true;
        ModelInstance far_;                      // z=-2, 12 from the camera
        far_.mesh = &quad; far_.texture = &green;
        far_.transform = Mat4::translate({0, 0, -2});
        far_.translucent = true;

        Framebuffer ab = render({ near_, far_ });
        Framebuffer ba = render({ far_, near_ });
        CHECK(framesEqual(ab, ba));              // the sort contract
        // Both layers composited at the centre (not pure background/either quad).
        Rgba c = ab.color.at(16,16);
        CHECK(c.r > 0 && c.g > 0 && c.b > 0 && c.b < 80);
        // Wrong-order canary: compositing near-then-far by hand differs, so the
        // identical frames above really are the sort's doing.
        Framebuffer wrong(32, 32);
        wrong.clear(Rgba{0, 0, 80, 255});
        ShadeLight sl;
        TexDrawOptions blendOpt; blendOpt.alphaBlend = true; blendOpt.depthWrite = false;
        rasterTexMesh(wrong, quad, mvp * near_.transform, red,   sl, blendOpt);
        rasterTexMesh(wrong, quad, mvp * far_.transform,  green, sl, blendOpt);
        CHECK(!framesEqual(ab, wrong));

        // Case 2: identical depth, different priorityPlane -- lower plane draws
        // first regardless of submission order.
        ModelInstance planeHi = near_;           // red, plane +1
        planeHi.transform = Mat4::identity();
        planeHi.priorityPlane = 1;
        ModelInstance planeLo = far_;            // green, plane -2
        planeLo.transform = Mat4::identity();
        planeLo.priorityPlane = -2;

        Framebuffer p0 = render({ planeHi, planeLo });
        Framebuffer p1 = render({ planeLo, planeHi });
        CHECK(framesEqual(p0, p1));
        // The higher plane (red) composites LAST, on top of the green.
        Rgba pc = p0.color.at(16,16);
        CHECK(pc.r > pc.g);

        // Opaque geometry still occludes deferred translucency (depth-test on):
        // an opaque quad nearer than both hides them at the centre.
        Image grey(1,1); grey.at(0,0) = Rgba{90, 90, 90, 255};
        ModelInstance blocker;
        blocker.mesh = &quad; blocker.texture = &grey;
        blocker.transform = Mat4::translate({0, 0, 5});
        Framebuffer occ = render({ near_, far_, blocker });
        Rgba oc = occ.color.at(16,16);
        CHECK(oc.r == oc.g && oc.g == oc.b);     // grey: no red/green leaked through
    }

    // ---- billboard bones: the quad faces the camera from any azimuth --------
    {
        std::vector<Bone> bones(1);
        bones[0].flags = M2BONE_SPHERICAL_BILLBOARD;

        Image white(1,1); white.at(0,0) = Rgba{255,255,255,255};
        ShadeLight flat; flat.ambient = {1,1,1}; flat.diffuse = {0,0,0}; flat.dir = {0,0,1};
        Mat4 proj = Mat4::perspective(60.0, 1.0, 0.1, 100.0);

        Vec3 eyes[2] = { {0, 0, 6}, {6, 0, 0} };  // two azimuths, 90deg apart
        int covered[2] = {0, 0};
        for (int e = 0; e < 2; ++e) {
            Mat4 view = Mat4::lookAt(eyes[e], {0,0,0}, {0,1,0});
            // model placement = identity, so modelView = view.
            std::vector<Mat4> pose = computePose(bones, 0, view);

            // Composed columns stay orthonormal...
            Vec3 c0{ pose[0].at(0,0), pose[0].at(1,0), pose[0].at(2,0) };
            Vec3 c1{ pose[0].at(0,1), pose[0].at(1,1), pose[0].at(2,1) };
            Vec3 c2{ pose[0].at(0,2), pose[0].at(1,2), pose[0].at(2,2) };
            CHECK_NEAR(length(c0), 1.0f, 1e-4);
            CHECK_NEAR(length(c1), 1.0f, 1e-4);
            CHECK_NEAR(length(c2), 1.0f, 1e-4);
            CHECK_NEAR(dot(c0, c1), 0.0f, 1e-4);
            CHECK_NEAR(dot(c1, c2), 0.0f, 1e-4);
            CHECK_NEAR(dot(c0, c2), 0.0f, 1e-4);
            // ...and forward equals the eye -> pivot direction in model space.
            Vec3 fwd = normalize(Vec3{0,0,0} - eyes[e]);
            CHECK_NEAR(c2.x, fwd.x, 1e-4);
            CHECK_NEAR(c2.y, fwd.y, 1e-4);
            CHECK_NEAR(c2.z, fwd.z, 1e-4);

            // Readback: a bone-space quad skinned by the billboarded pose covers
            // ~the same pixels from both views (it always faces the camera).
            TexMesh q;
            Vec2 corners[4] = { {-1.5f,-1.5f}, {1.5f,-1.5f}, {1.5f,1.5f}, {-1.5f,1.5f} };
            for (const Vec2& p : corners) {
                Vec4 wp = pose[0] * Vec4(p.x, p.y, 0.0f, 1.0f);
                q.vertices.push_back({ {wp.x, wp.y, wp.z}, {0,0,1}, {0,0} });
            }
            q.indices = { 0,1,2, 0,2,3 };

            Framebuffer fbb(48, 48);
            fbb.clear(Rgba{0,0,0,255});
            rasterTexMesh(fbb, q, proj * view, white, flat);
            for (const Rgba& p : fbb.color.pixels)
                if (p.r == 255) ++covered[e];
        }
        CHECK(covered[0] > 0);
        // Same distance + camera-facing from both azimuths -> ~equal coverage.
        int diff = covered[0] > covered[1] ? covered[0] - covered[1]
                                           : covered[1] - covered[0];
        CHECK(diff * 10 <= covered[0]);          // within 10%
    }
}
