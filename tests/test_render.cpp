#include "test.hpp"
#include "raster.hpp"
#include "m2_render.hpp"
#include "scene.hpp"
#include "terrain_render.hpp"
#include "modelmesh.hpp"
#include "terrain.hpp"
#include "wmo.hpp"
#include "m2.hpp"
#include "image.hpp"
#include "math.hpp"

#include <vector>

using namespace wf;

void test_render() {
    std::printf("[render]\n");

    // --- textured rasteriser: a quad sampling a 2x2 texture --------------
    {
        Image tex(2, 2);
        tex.at(0,0) = Rgba{255,0,0,255}; tex.at(1,0) = Rgba{0,255,0,255};
        tex.at(0,1) = Rgba{0,0,255,255}; tex.at(1,1) = Rgba{255,255,255,255};

        TexMesh quad;
        quad.vertices = {
            { {-2,-2,0}, {0,0,1}, {0,0} },
            { { 2,-2,0}, {0,0,1}, {1,0} },
            { { 2, 2,0}, {0,0,1}, {1,1} },
            { {-2, 2,0}, {0,0,1}, {0,1} },
        };
        quad.indices = { 0,1,2, 0,2,3 };

        Framebuffer fb(64, 64);
        fb.clear(Rgba{0,0,0,255});
        Mat4 view = Mat4::lookAt({0,0,6}, {0,0,0}, {0,1,0});
        Mat4 proj = Mat4::perspective(60.0, 1.0, 0.1, 100.0);
        // Light straight at the quad so shading ~ full.
        rasterTexMesh(fb, quad, proj * view, tex, {0,0,1});

        bool drewColour = false;
        for (const Rgba& p : fb.color.pixels)
            if (p.r > 40 || p.g > 40 || p.b > 40) { drewColour = true; break; }
        CHECK(drewColour);                       // textured surface rendered
        // Centre samples near the texture middle -> not pure background.
        CHECK(!(fb.color.at(32,32).r == 0 && fb.color.at(32,32).g == 0 && fb.color.at(32,32).b == 0));
    }

    // --- M2 skinning: a vertex bound to a moving bone follows it ---------
    {
        M2Model m;
        m.vertices.resize(3);
        for (auto& v : m.vertices) { v.normal = {0,0,1}; v.uv = {0,0}; }
        m.vertices[0].pos = {0,0,0}; m.vertices[0].boneWeights[0]=255; m.vertices[0].boneIndices[0]=0;
        m.vertices[1].pos = {1,0,0}; m.vertices[1].boneWeights[0]=255; m.vertices[1].boneIndices[0]=1;
        m.vertices[2].pos = {0,1,0}; m.vertices[2].boneWeights[0]=255; m.vertices[2].boneIndices[0]=0;
        m.vertexLookup = {0,1,2};
        m.triangles    = {0,1,2};

        // Static (no pose) reproduces the bind positions + uv.
        TexMesh bindMesh = skinM2(m, {});
        CHECK(bindMesh.vertices.size() == 3 && bindMesh.indices.size() == 3);
        CHECK_APPROX(bindMesh.vertices[1].position.x, 1.0f);

        // Pose: bone 0 identity, bone 1 translated +10 in x -> vertex 1 moves.
        std::vector<Mat4> pose = { Mat4::identity(), Mat4::translate({10,0,0}) };
        TexMesh posed = skinM2(m, pose);
        CHECK_APPROX(posed.vertices[0].position.x, 0.0f);   // bone 0 vertex stays
        CHECK_APPROX(posed.vertices[1].position.x, 11.0f);  // 1 + 10
    }

    // --- poseM2: sampling an animation yields a mesh ---------------------
    {
        M2Model m;
        m.vertices.resize(1); m.vertices[0].normal = {0,0,1};
        m.vertices[0].boneWeights[0] = 255; m.vertices[0].boneIndices[0] = 0;
        m.vertexLookup = {0}; m.triangles = {0,0,0};

        M2Animation anim;
        anim.sequences.push_back({}); anim.sequences[0].length = 1000;
        anim.bones.push_back({});                 // one root bone, no tracks
        TexMesh out = poseM2(m, anim, 0, 500);
        CHECK(out.vertices.size() == 1);
    }

    // --- scene: terrain + a textured instance + debug all render --------
    {
        Mesh terrain;
        terrain.vertices = { {{-10,-10,0},{0,0,1}}, {{10,-10,0},{0,0,1}},
                             {{10,10,0},{0,0,1}}, {{-10,10,0},{0,0,1}} };
        terrain.indices = { 0,1,2, 0,2,3 };

        Image tex(1,1); tex.at(0,0) = Rgba{200,120,40,255};
        TexMesh box;
        box.vertices = { {{-1,-1,2},{0,0,1},{0,0}}, {{1,-1,2},{0,0,1},{1,0}}, {{0,1,3},{0,0,1},{0.5f,1}} };
        box.indices = { 0,1,2 };

        DebugDraw dd;
        dd.aabb({-2,-2,0},{2,2,4}, Rgba{0,255,0,255}, DebugCategory::Trigger);

        Scene scene;
        scene.terrain = &terrain;
        scene.instances.push_back({ &box, &tex, Mat4::translate({0,0,0.5f}) });
        scene.debug = &dd;

        Framebuffer fb(96, 96);
        fb.clear(Rgba{10,10,14,255});
        Mat4 view = Mat4::lookAt({-14,-14,16}, {0,0,1}, {0,0,1});
        Mat4 proj = Mat4::perspective(55.0, 1.0, 0.5, 500.0);
        renderScene(fb, scene, proj * view);

        int lit = 0;
        for (const Rgba& p : fb.color.pixels)
            if (!(p.r==10 && p.g==10 && p.b==14)) ++lit;
        CHECK(lit > 200);                         // terrain + instance + overlay drawn
    }

    // --- terrain MCAL splat: blend a base + alpha-mapped overlay ----------
    {
        Image grass(1,1); grass.at(0,0) = Rgba{40,160,40,255};
        Image rock(1,1);  rock.at(0,0)  = Rgba{150,150,150,255};

        AlphaMap am;                              // layer-1 coverage
        std::vector<TerrainLayer> layers = { { &grass, nullptr }, { &rock, &am } };

        am.texels.fill(0);                        // no rock -> pure grass
        Rgba c0 = splatSample(layers, 0.5f, 0.5f, 1.0f);
        CHECK(c0.g > 120 && c0.r < 80);

        am.texels.fill(255);                      // full rock -> grey
        Rgba c1 = splatSample(layers, 0.5f, 0.5f, 1.0f);
        CHECK(c1.r > 120 && c1.g > 120 && c1.b > 120);

        am.texels.fill(128);                      // half -> blend between
        Rgba ch = splatSample(layers, 0.5f, 0.5f, 1.0f);
        CHECK(ch.r > c0.r && ch.r < c1.r);
    }

    // --- buildChunkTexMesh: 145 verts with chunk-space UVs ----------------
    {
        MapChunk mc;
        mc.position = {0,0,0};
        for (int i = 0; i < 145; ++i) mc.heights[i] = 0.0f;
        for (auto& n : mc.normals) n = {0,0,1};
        TexMesh tm = buildChunkTexMesh(mc, 32, 32);
        CHECK(tm.vertices.size() == 145);
        CHECK_APPROX(tm.vertices[0].uv.x, 0.0f);          // outer (0,0)
        CHECK_APPROX(tm.vertices[0].uv.y, 0.0f);
        CHECK_APPROX(tm.vertices[80].uv.x, 1.0f);         // outer (8,8)
        CHECK_APPROX(tm.vertices[80].uv.y, 1.0f);

        // It rasterises with the splat.
        Image grass(1,1); grass.at(0,0) = Rgba{40,160,40,255};
        std::vector<TerrainLayer> layers = { { &grass, nullptr } };
        Framebuffer fb(64,64); fb.clear(Rgba{0,0,0,255});
        Mat4 view = Mat4::lookAt({16,16,40}, {16,16,0}, {0,1,0});
        Mat4 proj = Mat4::perspective(60.0, 1.0, 0.5, 500.0);
        rasterTerrainSplat(fb, tm, proj*view, layers, 4.0f, {0,0,1});
        bool green = false;
        for (const Rgba& p : fb.color.pixels) if (p.g > 60 && p.g > p.r) { green = true; break; }
        CHECK(green);
    }

    // --- WMO group -> textured mesh preserves UVs -------------------------
    {
        WmoGroup g;
        g.vertices = { {0,0,0}, {1,0,0}, {0,1,0} };
        g.normals  = { {0,0,1}, {0,0,1}, {0,0,1} };
        g.uvs      = { {0,0}, {1,0}, {0,1} };
        g.indices  = { 0,1,2 };
        TexMesh tm = wmoGroupToTexMesh(g);
        CHECK(tm.vertices.size() == 3 && tm.indices.size() == 3);
        CHECK_APPROX(tm.vertices[1].uv.x, 1.0f);
        CHECK_APPROX(tm.vertices[2].uv.y, 1.0f);
    }
}
