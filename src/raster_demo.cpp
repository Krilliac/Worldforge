// Capstone: assemble a terrain mesh in WoW world space, project it through a
// camera, rasterise with the software rasteriser, and write a PNG. Proves the
// parse -> mesh -> world-transform -> camera -> pixels pipeline end to end with
// no GPU. The heightfield here is procedural (no copyrighted MPQ needed); the
// real MCNK -> mesh path is exercised separately by the unit tests.
#include <cmath>
#include <cstdio>
#include <vector>

#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"
#include "coords.hpp"

using namespace wf;

static float heightAt(float x, float y) {
    // Rolling hills + a ridge, in yards.
    return 40.0f * std::sin(x * 0.012f) * std::cos(y * 0.010f)
         + 18.0f * std::sin((x + y) * 0.03f)
         + 25.0f * std::exp(-((x-200.0f)*(x-200.0f) + (y-160.0f)*(y-160.0f)) / 9000.0f);
}

int main() {
    const int G = 160;          // grid resolution
    const float span = 420.0f;  // yards covered
    const float step = span / (G - 1);

    Mesh mesh;
    mesh.vertices.resize(static_cast<size_t>(G) * G);
    for (int j = 0; j < G; ++j) {
        for (int i = 0; i < G; ++i) {
            float x = i * step, y = j * step;
            float h = heightAt(x, y);
            // Normal from finite differences of the height field.
            float hx = heightAt(x + step, y) - heightAt(x - step, y);
            float hy = heightAt(x, y + step) - heightAt(x, y - step);
            Vec3 n = normalize(Vec3{ -hx, -hy, 2.0f * step });
            mesh.vertices[j * G + i] = { Vec3{ x, y, h }, n };
        }
    }
    for (int j = 0; j < G - 1; ++j) {
        for (int i = 0; i < G - 1; ++i) {
            uint32_t a = j*G + i, b = j*G + i+1, c = (j+1)*G + i, d = (j+1)*G + i+1;
            mesh.indices.insert(mesh.indices.end(), { a, b, c, b, d, c });
        }
    }

    const int W = 640, H = 480;
    Framebuffer fb(W, H);
    fb.clear(Rgba{ 24, 28, 40, 255 });   // dusk sky

    // Camera: above and to the south-east, looking at the field centre.
    Vec3 center{ span * 0.5f, span * 0.5f, 30.0f };
    Vec3 eye = center + Vec3{ -260.0f, -260.0f, 220.0f };
    Mat4 view = Mat4::lookAt(eye, center, Vec3{ 0, 0, 1 });   // Z-up world
    Mat4 proj = Mat4::perspective(55.0, double(W) / H, 1.0, 4000.0);

    rasterMesh(fb, mesh, proj * view, Vec3{ 0.5f, 0.4f, 0.8f });

    const char* out = "worldforge_terrain.png";
    if (!writePng(fb.color, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (%d triangles)\n", out, (int)(mesh.indices.size() / 3));
    return 0;
}
