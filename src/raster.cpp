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
    // Legacy grey terrain light: ambient 0.35, diffuse 0.65 (unchanged behaviour).
    ShadeLight sl;
    sl.dir = lightDir;
    sl.ambient = { 0.35f, 0.35f, 0.35f };
    sl.diffuse = { 0.65f, 0.65f, 0.65f };
    rasterMesh(fb, mesh, mvp, sl);
}

void rasterMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp, const ShadeLight& light) {
    int W = fb.color.width, H = fb.color.height;
    Vec3 L = normalize(light.dir);

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

                Vec3 lf = light.shade(n, L);                     // per-channel ambient+diffuse

                // Height tint: low = green, high = grey/white (slope-aware).
                float h = wp.z;
                float t01 = std::min(1.0f, std::max(0.0f, (h - (-50.0f)) / 200.0f));
                Rgba c;
                c.r = clamp8((60 + 150*t01) * lf.x);
                c.g = clamp8((110 + 90*t01) * lf.y);
                c.b = clamp8((50 + 140*t01) * lf.z);
                c.a = 255;

                dref = z;
                fb.color.at(px, py) = c;
            }
        }
    }
}

Rgba sampleTextureWrap(const Image& tex, float u, float v) {
    if (tex.width <= 0 || tex.height <= 0) return Rgba{255, 0, 255, 255};
    u -= std::floor(u); v -= std::floor(v);
    int tx = std::min(tex.width  - 1, (int)(u * tex.width));
    int ty = std::min(tex.height - 1, (int)(v * tex.height));
    return tex.at(tx, ty);
}

void rasterTexMesh(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                   const Image& tex, Vec3 lightDir, bool alphaBlend) {
    // Legacy grey model light (ambient 0.4, diffuse 0.6) -- the ShadeLight default.
    ShadeLight sl; sl.dir = lightDir;
    rasterTexMesh(fb, mesh, mvp, tex, sl, alphaBlend);
}

void rasterTexMesh(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                   const Image& tex, const ShadeLight& light, bool alphaBlend, float alphaMul) {
    // Legacy behaviour: the blended path never writes depth, the opaque one does.
    TexDrawOptions opt;
    opt.alphaBlend = alphaBlend;
    opt.alphaMul   = alphaMul;
    opt.depthWrite = !alphaBlend;
    rasterTexMesh(fb, mesh, mvp, tex, light, opt);
}

void rasterTexMesh(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                   const Image& tex, const ShadeLight& light, const TexDrawOptions& opt) {
    int W = fb.color.width, H = fb.color.height;
    float alphaMul = opt.alphaMul;
    if (alphaMul < 0.0f) alphaMul = 0.0f;
    if (alphaMul > 1.0f) alphaMul = 1.0f;
    Vec3 L = normalize(light.dir);

    struct VOut { Vec4 clip; bool valid; };
    std::vector<VOut> vo(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vec4 c = mvp * Vec4(mesh.vertices[i].position, 1.0f);
        vo[i] = { c, c.w > 1e-4f };
    }
    auto toScreen = [&](const Vec4& c, float& sx, float& sy, float& sz, float& invw) {
        invw = 1.0f / c.w;
        sx = (c.x * invw * 0.5f + 0.5f) * W;
        sy = (1.0f - (c.y * invw * 0.5f + 0.5f)) * H;
        sz = c.z * invw;
    };

    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t+1], i2 = mesh.indices[t+2];
        if (!vo[i0].valid || !vo[i1].valid || !vo[i2].valid) continue;

        float x0,y0,z0,iw0, x1,y1,z1,iw1, x2,y2,z2,iw2;
        toScreen(vo[i0].clip, x0,y0,z0,iw0);
        toScreen(vo[i1].clip, x1,y1,z1,iw1);
        toScreen(vo[i2].clip, x2,y2,z2,iw2);
        float area = edge(x0,y0, x1,y1, x2,y2);
        if (std::fabs(area) < 1e-6f) continue;

        const TexVertex& V0 = mesh.vertices[i0];
        const TexVertex& V1 = mesh.vertices[i1];
        const TexVertex& V2 = mesh.vertices[i2];

        int minX = std::max(0,   (int)std::floor(std::min({x0,x1,x2})));
        int maxX = std::min(W-1, (int)std::ceil (std::max({x0,x1,x2})));
        int minY = std::max(0,   (int)std::floor(std::min({y0,y1,y2})));
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

                float iw = l0*iw0 + l1*iw1 + l2*iw2;
                float u = (l0*V0.uv.x*iw0 + l1*V1.uv.x*iw1 + l2*V2.uv.x*iw2) / iw;
                float v = (l0*V0.uv.y*iw0 + l1*V1.uv.y*iw1 + l2*V2.uv.y*iw2) / iw;
                if (opt.useUvTransform) {
                    // Animated UV matrix (texture transform): (u,v,0,1) row.
                    Vec4 uv = opt.uvTransform * Vec4(u, v, 0.0f, 1.0f);
                    u = uv.x; v = uv.y;
                }
                Rgba texel = sampleTextureWrap(tex, u, v);
                if (texel.a < 8) continue;                 // alpha-test cutout

                Vec3 n = (V0.normal*(l0*iw0) + V1.normal*(l1*iw1) + V2.normal*(l2*iw2)) * (1.0f/iw);
                n = normalize(n);
                Vec3 lf = light.shade(n, L);

                // Perspective-correct per-vertex colour (default white -> 1.0, no
                // effect). WMO interiors carry MOCV baked light here.
                float vr = (l0*V0.color.r*iw0 + l1*V1.color.r*iw1 + l2*V2.color.r*iw2) / (iw*255.0f);
                float vg = (l0*V0.color.g*iw0 + l1*V1.color.g*iw1 + l2*V2.color.g*iw2) / (iw*255.0f);
                float vb = (l0*V0.color.b*iw0 + l1*V1.color.b*iw1 + l2*V2.color.b*iw2) / (iw*255.0f);

                Rgba c;
                c.r = clamp8(texel.r * lf.x * vr);
                c.g = clamp8(texel.g * lf.y * vg);
                c.b = clamp8(texel.b * lf.z * vb);
                if (opt.alphaBlend) {
                    // Composite over the framebuffer (translucent). alphaMul
                    // fades the whole object (distance fade) atop the texel's
                    // own alpha.
                    float a = (texel.a / 255.0f) * alphaMul;
                    Rgba& d = fb.color.at(px, py);
                    d.r = clamp8(c.r * a + d.r * (1.0f - a));
                    d.g = clamp8(c.g * a + d.g * (1.0f - a));
                    d.b = clamp8(c.b * a + d.b * (1.0f - a));
                } else {
                    c.a = 255;
                    fb.color.at(px, py) = c;
                }
                if (opt.depthWrite) dref = z;
            }
        }
    }
}

void rasterLiquidMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp,
                      Rgba tint, Vec3 lightDir, bool emissive) {
    // Legacy water sheen is a neutral scalar 0.5 + 0.5*N·L: a ShadeLight with
    // ambient 0.5 / diffuse 0.5 (white) reproduces it exactly.
    ShadeLight sl; sl.dir = lightDir;
    sl.ambient = { 0.5f, 0.5f, 0.5f };
    sl.diffuse = { 0.5f, 0.5f, 0.5f };
    rasterLiquidMesh(fb, mesh, mvp, tint, sl, emissive);
}

void rasterLiquidMesh(Framebuffer& fb, const Mesh& mesh, const Mat4& mvp,
                      Rgba tint, const ShadeLight& light, bool emissive) {
    int W = fb.color.width, H = fb.color.height;
    Vec3 L = normalize(light.dir);
    const float alpha = tint.a / 255.0f;

    struct VOut { Vec4 clip; bool valid; };
    std::vector<VOut> vo(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vec4 c = mvp * Vec4(mesh.vertices[i].position, 1.0f);
        vo[i] = { c, c.w > 1e-4f };
    }
    auto toScreen = [&](const Vec4& c, float& sx, float& sy, float& sz, float& invw) {
        invw = 1.0f / c.w;
        sx = (c.x * invw * 0.5f + 0.5f) * W;
        sy = (1.0f - (c.y * invw * 0.5f + 0.5f)) * H;
        sz = c.z * invw;
    };

    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t+1], i2 = mesh.indices[t+2];
        if (!vo[i0].valid || !vo[i1].valid || !vo[i2].valid) continue;

        float x0,y0,z0,iw0, x1,y1,z1,iw1, x2,y2,z2,iw2;
        toScreen(vo[i0].clip, x0,y0,z0,iw0);
        toScreen(vo[i1].clip, x1,y1,z1,iw1);
        toScreen(vo[i2].clip, x2,y2,z2,iw2);
        float area = edge(x0,y0, x1,y1, x2,y2);
        if (std::fabs(area) < 1e-6f) continue;

        const Vertex& V0 = mesh.vertices[i0];
        const Vertex& V1 = mesh.vertices[i1];
        const Vertex& V2 = mesh.vertices[i2];

        int minX = std::max(0,   (int)std::floor(std::min({x0,x1,x2})));
        int maxX = std::min(W-1, (int)std::ceil (std::max({x0,x1,x2})));
        int minY = std::max(0,   (int)std::floor(std::min({y0,y1,y2})));
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
                if (z >= dref) continue;          // depth-test only; no depth write

                Vec3 lf{ 1.0f, 1.0f, 1.0f };
                if (!emissive) {
                    float iw = l0*iw0 + l1*iw1 + l2*iw2;
                    Vec3 n = (V0.normal*(l0*iw0) + V1.normal*(l1*iw1) + V2.normal*(l2*iw2)) * (1.0f/iw);
                    n = normalize(n);
                    lf = light.shade(n, L);                        // per-channel soft sheen
                }

                Rgba& d = fb.color.at(px, py);
                d.r = clamp8(tint.r * lf.x * alpha + d.r * (1.0f - alpha));
                d.g = clamp8(tint.g * lf.y * alpha + d.g * (1.0f - alpha));
                d.b = clamp8(tint.b * lf.z * alpha + d.b * (1.0f - alpha));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Debug overlay: lines, point markers, translucent triangles.
// ---------------------------------------------------------------------------
namespace {

struct Proj { float x, y, z; bool valid; };  // screen px + NDC depth

inline Proj project(const Mat4& mvp, const Vec3& p, int W, int H) {
    Vec4 c = mvp * Vec4(p, 1.0f);
    if (c.w <= 1e-4f) return {0, 0, 0, false};         // behind / on the near plane
    float inv = 1.0f / c.w;
    return {
        (c.x * inv * 0.5f + 0.5f) * W,
        (1.0f - (c.y * inv * 0.5f + 0.5f)) * H,
        c.z * inv,
        true
    };
}

inline void blend(Rgba& dst, Rgba src) {
    if (src.a == 255) { dst = src; return; }
    float a = src.a / 255.0f;
    dst.r = clamp8(src.r * a + dst.r * (1 - a));
    dst.g = clamp8(src.g * a + dst.g * (1 - a));
    dst.b = clamp8(src.b * a + dst.b * (1 - a));
}

// Plot one pixel with optional depth test (z biased so coincident overlay wins).
inline void plot(Framebuffer& fb, int x, int y, float z, Rgba c,
                 const DebugDrawOptions& opt) {
    if (x < 0 || y < 0 || x >= fb.color.width || y >= fb.color.height) return;
    float& dref = fb.depth[(size_t)y * fb.color.width + x];
    if (opt.depthTest && z - 1e-4f > dref) return;     // hidden behind geometry
    blend(fb.color.at(x, y), c);
    if (opt.writeDepth) dref = z;
}

void drawLine(Framebuffer& fb, const Proj& a, const Proj& b, Rgba c,
              const DebugDrawOptions& opt) {
    float dx = b.x - a.x, dy = b.y - a.y;
    int steps = static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy))));
    if (steps <= 0) { plot(fb, (int)a.x, (int)a.y, a.z, c, opt); return; }
    float ix = dx / steps, iy = dy / steps, iz = (b.z - a.z) / steps;
    float x = a.x, y = a.y, z = a.z;
    for (int i = 0; i <= steps; ++i) {
        plot(fb, (int)std::lround(x), (int)std::lround(y), z, c, opt);
        x += ix; y += iy; z += iz;
    }
}

} // namespace

void rasterDebug(Framebuffer& fb, const DebugDraw& dd, const Mat4& mvp,
                 const DebugDrawOptions& opt) {
    const int W = fb.color.width, H = fb.color.height;

    for (uint32_t ci = 0; ci < static_cast<uint32_t>(DebugCategory::Count); ++ci) {
        DebugCategory cat = static_cast<DebugCategory>(ci);
        if (!dd.categoryEnabled(cat)) continue;
        const DebugDraw::Buffers& buf = dd.categoryBuffers(cat);

        // Translucent triangles first (so lines/markers read on top).
        for (size_t i = 0; i + 2 < buf.tris.size(); i += 3) {
            Proj p0 = project(mvp, buf.tris[i].pos,   W, H);
            Proj p1 = project(mvp, buf.tris[i+1].pos, W, H);
            Proj p2 = project(mvp, buf.tris[i+2].pos, W, H);
            if (!p0.valid || !p1.valid || !p2.valid) continue;
            int minX = std::max(0, (int)std::floor(std::min({p0.x,p1.x,p2.x})));
            int maxX = std::min(W-1, (int)std::ceil (std::max({p0.x,p1.x,p2.x})));
            int minY = std::max(0, (int)std::floor(std::min({p0.y,p1.y,p2.y})));
            int maxY = std::min(H-1, (int)std::ceil (std::max({p0.y,p1.y,p2.y})));
            float area = edge(p0.x,p0.y, p1.x,p1.y, p2.x,p2.y);
            if (std::fabs(area) < 1e-6f) continue;
            Rgba col = buf.tris[i].color;
            for (int py = minY; py <= maxY; ++py)
                for (int px = minX; px <= maxX; ++px) {
                    float fx = px + 0.5f, fy = py + 0.5f;
                    float w0 = edge(p1.x,p1.y, p2.x,p2.y, fx,fy);
                    float w1 = edge(p2.x,p2.y, p0.x,p0.y, fx,fy);
                    float w2 = edge(p0.x,p0.y, p1.x,p1.y, fx,fy);
                    bool in = (w0>=0&&w1>=0&&w2>=0) || (w0<=0&&w1<=0&&w2<=0);
                    if (!in) continue;
                    float z = (w0*p0.z + w1*p1.z + w2*p2.z) / area;
                    plot(fb, px, py, z, col, opt);
                }
        }

        // Lines.
        for (size_t i = 0; i + 1 < buf.lines.size(); i += 2) {
            Proj a = project(mvp, buf.lines[i].pos,   W, H);
            Proj b = project(mvp, buf.lines[i+1].pos, W, H);
            if (!a.valid || !b.valid) continue;          // crosses the near plane
            drawLine(fb, a, b, buf.lines[i].color, opt);
        }

        // Point markers: a filled square of pointSize.
        const int r = std::max(0, opt.pointSize / 2);
        for (const DebugVertex& pt : buf.points) {
            Proj p = project(mvp, pt.pos, W, H);
            if (!p.valid) continue;
            int cx = (int)std::lround(p.x), cy = (int)std::lround(p.y);
            for (int oy = -r; oy <= r; ++oy)
                for (int ox = -r; ox <= r; ++ox)
                    plot(fb, cx + ox, cy + oy, p.z, pt.color, opt);
        }
    }
}

void fillSkyGradient(Framebuffer& fb, Rgba top, Rgba horizon) {
    const int W = fb.color.width, H = fb.color.height;
    for (float& d : fb.depth) d = std::numeric_limits<float>::infinity();
    for (int py = 0; py < H; ++py) {
        const float t = H > 1 ? float(py) / float(H - 1) : 1.0f;
        auto mix = [&](uint8_t a, uint8_t b) { return (uint8_t)(a + (b - a) * t + 0.5f); };
        const Rgba row{ mix(top.r, horizon.r), mix(top.g, horizon.g),
                        mix(top.b, horizon.b), 255 };
        for (int px = 0; px < W; ++px) fb.color.at(px, py) = row;
    }
}

void applyDistanceFog(Framebuffer& fb, Rgba fog, float maxFog) {
    const int W = fb.color.width, H = fb.color.height;
    // Normalise fog over the depths actually drawn this frame, so the effect is
    // stable across camera/scene scale without needing world-space distances.
    float zmin = std::numeric_limits<float>::infinity();
    float zmax = -std::numeric_limits<float>::infinity();
    for (float d : fb.depth)
        if (std::isfinite(d)) { zmin = std::min(zmin, d); zmax = std::max(zmax, d); }
    if (!(zmax > zmin)) return;                     // empty scene or single depth
    const float span = zmax - zmin;
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            const float d = fb.depth[(size_t)py * W + px];
            if (!std::isfinite(d)) continue;        // sky pixel: keep the gradient
            const float f = std::clamp((d - zmin) / span, 0.0f, 1.0f) * maxFog;
            Rgba& c = fb.color.at(px, py);
            c.r = (uint8_t)(c.r + (fog.r - c.r) * f);
            c.g = (uint8_t)(c.g + (fog.g - c.g) * f);
            c.b = (uint8_t)(c.b + (fog.b - c.b) * f);
        }
}

} // namespace wf
