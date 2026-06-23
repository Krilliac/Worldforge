#include "raster.hpp"
#include <algorithm>
#include <cmath>

namespace wf {
namespace {

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

inline uint8_t clamp8(float v) {
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return static_cast<uint8_t>(v + 0.5f);
}

} // namespace

void fillTriangleSolid(Framebuffer& fb, const ScreenVert& a, const ScreenVert& b,
                       const ScreenVert& c, Rgba color) {
    int W = fb.color.width, H = fb.color.height;
    int minX = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})));
    int maxX = std::min(W - 1, (int)std::ceil(std::max({a.x, b.x, c.x})));
    int minY = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
    int maxY = std::min(H - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));

    float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
    if (std::fabs(area) < 1e-6f) return;

    for (int py = minY; py <= maxY; ++py) {
        for (int px = minX; px <= maxX; ++px) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float w0 = edge(b.x, b.y, c.x, c.y, fx, fy);
            float w1 = edge(c.x, c.y, a.x, a.y, fx, fy);
            float w2 = edge(a.x, a.y, b.x, b.y, fx, fy);
            // Inside if all weights share the triangle's sign.
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
            float z = l0 * a.z + l1 * b.z + l2 * c.z;
            float& dref = fb.depth[(size_t)py * W + px];
            if (z < dref) { dref = z; fb.color.at(px, py) = color; }
        }
    }
}

void rasterMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp, Vec3 lightDir) {
    int W = fb.color.width, H = fb.color.height;
    Vec3 L = normalize(lightDir);

    // Precompute clip-space for all vertices.
    struct VOut { Vec4 clip; bool valid; };
    std::vector<VOut> vo(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vec4 c = mvp * Vec4(mesh.vertices[i].position, 1.0f);
        vo[i] = { c, c.w > 1e-4f };
    }

    auto toScreen = [&](const Vec4& c, float& sx, float& sy, float& sz, float& invw) {
        invw = 1.0f / c.w;
        float nx = c.x * invw, ny = c.y * invw, nz = c.z * invw;
        sx = (nx * 0.5f + 0.5f) * W;
        sy = (1.0f - (ny * 0.5f + 0.5f)) * H;
        sz = nz;
    };

    for (size_t t = 0; t + 2 < mesh.indices.size() + 0; t += 3) {
        if (t + 2 >= mesh.indices.size()) break;
        uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t+1], i2 = mesh.indices[t+2];
        if (!vo[i0].valid || !vo[i1].valid || !vo[i2].valid) continue;  // crosses near plane

        float x0,y0,z0,iw0, x1,y1,z1,iw1, x2,y2,z2,iw2;
        toScreen(vo[i0].clip, x0,y0,z0,iw0);
        toScreen(vo[i1].clip, x1,y1,z1,iw1);
        toScreen(vo[i2].clip, x2,y2,z2,iw2);

        float area = edge(x0,y0, x1,y1, x2,y2);
        if (std::fabs(area) < 1e-6f) continue;

        const Vertex& V0 = mesh.vertices[i0];
        const Vertex& V1 = mesh.vertices[i1];
        const Vertex& V2 = mesh.vertices[i2];

        int minX = std::max(0, (int)std::floor(std::min({x0,x1,x2})));
        int maxX = std::min(W-1, (int)std::ceil (std::max({x0,x1,x2})));
        int minY = std::max(0, (int)std::floor(std::min({y0,y1,y2})));
        int maxY = std::min(H-1, (int)std::ceil (std::max({y0,y1,y2})));

        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                float fx = px + 0.5f, fy = py + 0.5f;
                float w0 = edge(x1,y1, x2,y2, fx,fy);
                float w1 = edge(x2,y2, x0,y0, fx,fy);
                float w2 = edge(x0,y0, x1,y1, fx,fy);
                bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
                if (!inside) continue;
                float l0 = w0/area, l1 = w1/area, l2 = w2/area;

                float z = l0*z0 + l1*z1 + l2*z2;
                float& dref = fb.depth[(size_t)py*W + px];
                if (z >= dref) continue;

                // Perspective-correct interpolation of world attributes.
                float iw = l0*iw0 + l1*iw1 + l2*iw2;
                Vec3 n = (V0.normal*(l0*iw0) + V1.normal*(l1*iw1) + V2.normal*(l2*iw2)) * (1.0f/iw);
                Vec3 wp = (V0.position*(l0*iw0) + V1.position*(l1*iw1) + V2.position*(l2*iw2)) * (1.0f/iw);
                n = normalize(n);

                float diff = std::max(0.0f, dot(n, L));
                float light = 0.35f + 0.65f * diff;              // ambient + diffuse

                // Height tint: low = green, high = grey/white (slope-aware).
                float h = wp.z;
                float t01 = std::min(1.0f, std::max(0.0f, (h - (-50.0f)) / 200.0f));
                Rgba c;
                c.r = clamp8((60 + 150*t01) * light);
                c.g = clamp8((110 + 90*t01) * light);
                c.b = clamp8((50 + 140*t01) * light);
                c.a = 255;

                dref = z;
                fb.color.at(px, py) = c;
            }
        }
    }
}

} // namespace wf
