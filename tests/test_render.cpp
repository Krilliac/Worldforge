#include "test.hpp"
#include "raster.hpp"
#include "m2_render.hpp"
#include "scene.hpp"
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
}
