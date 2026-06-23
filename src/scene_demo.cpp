// Scene demo: terrain + textured model instances (M2-style) + a debug overlay,
// all rendered through one camera by the scene compositor -> PNG. Proves the
// "render all the objects" path on the CPU. Procedural assets (no copyrighted
// MPQ); the real path is parseM2/parseWmoGroup -> skinM2 -> the same Scene.
#include <cmath>
#include <cstdio>
#include <vector>

#include "scene.hpp"
#include "raster.hpp"
#include "m2_render.hpp"
#include "debugdraw.hpp"
#include "image.hpp"
#include "math.hpp"
#include "editor/demo_assets.hpp"

using namespace wf;

static float heightAt(float x, float y) {
    return 14.0f * std::sin(x * 0.05f) * std::cos(y * 0.04f);
}

int main() {
    const int W = 960, H = 600;
    Framebuffer fb(W, H);
    fb.clear(Rgba{ 20, 24, 34, 255 });

    // Terrain.
    const int G = 70; const float span = 160.0f; const float step = span / (G - 1);
    Mesh terrain; terrain.vertices.resize((size_t)G*G);
    for (int j = 0; j < G; ++j) for (int i = 0; i < G; ++i) {
        float x = i*step, y = j*step, h = heightAt(x,y);
        float hx = heightAt(x+step,y)-heightAt(x-step,y), hy = heightAt(x,y+step)-heightAt(x,y-step);
        terrain.vertices[j*G+i] = { Vec3{x,y,h}, normalize(Vec3{-hx,-hy,2*step}) };
    }
    for (int j = 0; j < G-1; ++j) for (int i = 0; i < G-1; ++i) {
        uint32_t a=j*G+i, b=j*G+i+1, c=(j+1)*G+i, d=(j+1)*G+i+1;
        terrain.indices.insert(terrain.indices.end(), {a,b,c,b,d,c});
    }

    // A handful of animated "doodad" instances posed at varying times.
    M2Model model = editor::makeDemoModel();
    M2Animation anim = editor::makeDemoAnim();
    Image tex = editor::makeCheckerTexture();

    std::vector<TexMesh> meshes;          // keep alive: instances point into these
    std::vector<Mat4>    xforms;
    for (int k = 0; k < 12; ++k) {
        float x = 30 + (k % 4) * 30.0f;
        float y = 30 + (k / 4) * 30.0f;
        meshes.push_back(poseM2(model, anim, 0, (uint32_t)((k * 137) % 1000)));
        xforms.push_back(Mat4::translate({ x, y, heightAt(x,y) }) * Mat4::scale({3,3,3}));
    }

    DebugDraw dd;
    for (size_t k = 0; k < xforms.size(); ++k) {
        Vec4 o = xforms[k] * Vec4(0,0,0,1);
        dd.cross({o.x, o.y, o.z}, 3.0f, Rgba{255,220,60,255}, DebugCategory::Marker);
    }

    Scene scene;
    scene.terrain = &terrain;
    scene.debug = &dd;
    for (size_t k = 0; k < meshes.size(); ++k)
        scene.instances.push_back({ &meshes[k], &tex, xforms[k] });

    Vec3 center{ span*0.5f, span*0.5f, 6.0f };
    Mat4 view = Mat4::lookAt(center + Vec3{-90,-90,80}, center, {0,0,1});
    Mat4 proj = Mat4::perspective(55.0, double(W)/H, 1.0, 2000.0);
    renderScene(fb, scene, proj * view);

    const char* out = "worldforge_scene.png";
    if (!writePng(fb.color, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (%d instances, %d terrain tris)\n",
                out, (int)scene.instances.size(), (int)(terrain.indices.size()/3));
    return 0;
}
