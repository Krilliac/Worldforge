// Debug-overlay capstone: render a procedural terrain tile, then draw the debug
// visualisation layers on top of it -- terrain wireframe, a creature waypoint
// path, a trigger volume (AABB + sphere), a line-of-sight / collision ray with
// a hit marker, surface normals, and a world grid -- exactly the server-side
// data WorldForge's viewport visualises (mangoszero `.debug vis ...`). Proves
// the mesh + overlay pipeline end to end with no GPU. Output: a PNG.
#include <cmath>
#include <cstdio>
#include <vector>

#include "raster.hpp"
#include "debugdraw.hpp"
#include "image.hpp"
#include "math.hpp"

using namespace wf;

static float heightAt(float x, float y) {
    return 30.0f * std::sin(x * 0.018f) * std::cos(y * 0.015f)
         + 12.0f * std::sin((x + y) * 0.04f);
}

int main() {
    const int G = 96;
    const float span = 300.0f;
    const float step = span / (G - 1);

    Mesh mesh;
    mesh.vertices.resize(static_cast<size_t>(G) * G);
    for (int j = 0; j < G; ++j)
        for (int i = 0; i < G; ++i) {
            float x = i * step, y = j * step, h = heightAt(x, y);
            float hx = heightAt(x + step, y) - heightAt(x - step, y);
            float hy = heightAt(x, y + step) - heightAt(x, y - step);
            Vec3 n = normalize(Vec3{ -hx, -hy, 2.0f * step });
            mesh.vertices[j * G + i] = { Vec3{ x, y, h }, n };
        }
    for (int j = 0; j < G - 1; ++j)
        for (int i = 0; i < G - 1; ++i) {
            uint32_t a = j*G + i, b = j*G + i+1, c = (j+1)*G + i, d = (j+1)*G + i+1;
            mesh.indices.insert(mesh.indices.end(), { a, b, c, b, d, c });
        }

    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    fb.clear(Rgba{ 18, 20, 28, 255 });

    Vec3 center{ span * 0.5f, span * 0.5f, 10.0f };
    Vec3 eye = center + Vec3{ -200.0f, -200.0f, 170.0f };
    Mat4 view = Mat4::lookAt(eye, center, Vec3{ 0, 0, 1 });
    Mat4 proj = Mat4::perspective(55.0, double(W) / H, 1.0, 3000.0);
    Mat4 mvp = proj * view;

    rasterMesh(fb, mesh, mvp, Vec3{ 0.5f, 0.4f, 0.8f });

    // ---- assemble the debug overlay -----------------------------------------
    DebugDraw dd;
    auto groundZ = [&](float x, float y) { return heightAt(x, y) + 0.5f; };

    // Terrain wireframe (a decimated copy so the lines stay readable).
    Mesh wire;
    const int WG = 24, ws = (G - 1) / (WG - 1);
    wire.vertices.resize(static_cast<size_t>(WG) * WG);
    for (int j = 0; j < WG; ++j)
        for (int i = 0; i < WG; ++i)
            wire.vertices[j*WG + i] = mesh.vertices[(j*ws)*G + (i*ws)];
    for (int j = 0; j < WG - 1; ++j)
        for (int i = 0; i < WG - 1; ++i) {
            uint32_t a = j*WG+i, b = j*WG+i+1, c = (j+1)*WG+i, d = (j+1)*WG+i+1;
            wire.indices.insert(wire.indices.end(), { a,b,c, b,d,c });
        }
    dd.wireframe(wire, Rgba{ 70, 90, 120, 200 }, DebugCategory::TerrainWire);

    // Surface normals (sparse).
    Mesh normalsSrc;
    for (int j = 4; j < G - 4; j += 12)
        for (int i = 4; i < G - 4; i += 12)
            normalsSrc.vertices.push_back(mesh.vertices[j*G + i]);
    dd.normals(normalsSrc, 12.0f, Rgba{ 120, 220, 255, 255 }, DebugCategory::Normal);

    // A creature waypoint path snaking across the tile (DV_PATH).
    std::vector<Vec3> path;
    for (int k = 0; k <= 10; ++k) {
        float x = 30.0f + k * 24.0f;
        float y = 150.0f + 70.0f * std::sin(k * 0.6f);
        path.push_back({ x, y, groundZ(x, y) + 2.0f });
    }
    dd.path(path, Rgba{ 255, 220, 60, 255 }, DebugCategory::Waypoint, true);

    // A trigger volume: AABB + an inscribed sphere (DV-style trigger).
    dd.aabb({ 200, 60, groundZ(200,60) }, { 250, 110, groundZ(225,85) + 40 },
            Rgba{ 80, 255, 140, 255 }, DebugCategory::Trigger);
    dd.sphere({ 225, 85, groundZ(225,85) + 20 }, 22.0f,
              Rgba{ 80, 255, 140, 120 }, DebugCategory::Trigger, 20);

    // A line-of-sight / collision ray with a hit marker (DV_LOS / DV_HITPOINT).
    Vec3 from{ 60, 60, groundZ(60,60) + 25 };
    Vec3 hit { 170, 120, groundZ(170,120) + 3 };
    dd.arrow(from, hit, Rgba{ 255, 90, 90, 255 }, DebugCategory::Collision, 8.0f);
    dd.point(hit, Rgba{ 255, 40, 40, 255 }, DebugCategory::Marker);

    // World reference grid on the z=0 plane (DV_CELL-ish).
    dd.grid(center, span * 0.5f, 33.333f, Rgba{ 40, 45, 60, 120 },
            Rgba{ 70, 80, 110, 160 }, 4.0f);

    DebugDrawOptions opt;
    opt.depthTest = true;   // overlay is occluded by hills in front of it
    rasterDebug(fb, dd, mvp, opt);

    DebugDraw::Stats s = dd.stats();
    const char* out = "worldforge_debug.png";
    if (!writePng(fb.color, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (%d lines, %d tris, %d points)\n",
                out, s.lines, s.triangles, s.points);
    return 0;
}
