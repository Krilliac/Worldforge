#include "editor/SoftwareImGui.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace wf::editor {

namespace {
inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
inline uint8_t mul8(uint8_t a, uint8_t b) { return static_cast<uint8_t>((int(a) * int(b)) / 255); }

// Nearest-sample the RGBA32 font atlas at normalised (u,v).
void sampleAtlas(const unsigned char* px, int w, int h, float u, float v, uint8_t out[4]) {
    if (!px || w <= 0 || h <= 0) { out[0]=out[1]=out[2]=out[3]=255; return; }
    int tx = std::clamp(int(u * w), 0, w - 1);
    int ty = std::clamp(int(v * h), 0, h - 1);
    const unsigned char* t = px + (size_t(ty) * w + tx) * 4;
    out[0]=t[0]; out[1]=t[1]; out[2]=t[2]; out[3]=t[3];
}
} // namespace

void renderImGuiSoftware(const ImDrawData* dd, Image& target,
                         const unsigned char* atlasPixels, int atlasW, int atlasH) {
    if (!dd) return;
    const int W = target.width, H = target.height;

    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        const ImDrawVert* vtx = cl->VtxBuffer.Data;
        const ImDrawIdx*  idx = cl->IdxBuffer.Data;

        for (int c = 0; c < cl->CmdBuffer.Size; ++c) {
            const ImDrawCmd& cmd = cl->CmdBuffer[c];
            if (cmd.UserCallback) continue;

            // Clip rectangle (screen space) intersected with the target.
            int clipX0 = std::max(0, (int)cmd.ClipRect.x);
            int clipY0 = std::max(0, (int)cmd.ClipRect.y);
            int clipX1 = std::min(W, (int)cmd.ClipRect.z);
            int clipY1 = std::min(H, (int)cmd.ClipRect.w);
            if (clipX1 <= clipX0 || clipY1 <= clipY0) continue;

            for (unsigned int e = 0; e + 2 < cmd.ElemCount + 0u && e + 2 < cmd.ElemCount; e += 3) {
                const ImDrawVert& v0 = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + e + 0]];
                const ImDrawVert& v1 = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + e + 1]];
                const ImDrawVert& v2 = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + e + 2]];

                float minXf = std::min({v0.pos.x, v1.pos.x, v2.pos.x});
                float maxXf = std::max({v0.pos.x, v1.pos.x, v2.pos.x});
                float minYf = std::min({v0.pos.y, v1.pos.y, v2.pos.y});
                float maxYf = std::max({v0.pos.y, v1.pos.y, v2.pos.y});
                int minX = std::max(clipX0, (int)std::floor(minXf));
                int maxX = std::min(clipX1 - 1, (int)std::ceil(maxXf));
                int minY = std::max(clipY0, (int)std::floor(minYf));
                int maxY = std::min(clipY1 - 1, (int)std::ceil(maxYf));
                if (maxX < minX || maxY < minY) continue;

                float area = edge(v0.pos.x, v0.pos.y, v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y);
                if (std::fabs(area) < 1e-6f) continue;
                float inv = 1.0f / area;

                for (int py = minY; py <= maxY; ++py) {
                    for (int px = minX; px <= maxX; ++px) {
                        float fx = px + 0.5f, fy = py + 0.5f;
                        float w0 = edge(v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y, fx, fy) * inv;
                        float w1 = edge(v2.pos.x, v2.pos.y, v0.pos.x, v0.pos.y, fx, fy) * inv;
                        float w2 = edge(v0.pos.x, v0.pos.y, v1.pos.x, v1.pos.y, fx, fy) * inv;
                        bool in = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
                        if (!in) continue;

                        // Interpolate vertex colour (RGBA8 packed) and uv.
                        auto col = [&](ImU32 c, int sh){ return float((c >> sh) & 0xFF); };
                        uint8_t vr = (uint8_t)(w0*col(v0.col,0)  + w1*col(v1.col,0)  + w2*col(v2.col,0));
                        uint8_t vg = (uint8_t)(w0*col(v0.col,8)  + w1*col(v1.col,8)  + w2*col(v2.col,8));
                        uint8_t vb = (uint8_t)(w0*col(v0.col,16) + w1*col(v1.col,16) + w2*col(v2.col,16));
                        uint8_t va = (uint8_t)(w0*col(v0.col,24) + w1*col(v1.col,24) + w2*col(v2.col,24));
                        float u = w0*v0.uv.x + w1*v1.uv.x + w2*v2.uv.x;
                        float v = w0*v0.uv.y + w1*v1.uv.y + w2*v2.uv.y;

                        uint8_t tex[4];
                        sampleAtlas(atlasPixels, atlasW, atlasH, u, v, tex);
                        uint8_t sr = mul8(vr, tex[0]), sg = mul8(vg, tex[1]);
                        uint8_t sb = mul8(vb, tex[2]), sa = mul8(va, tex[3]);
                        if (sa == 0) continue;

                        Rgba& d = target.at(px, py);
                        float a = sa / 255.0f;
                        d.r = (uint8_t)(sr * a + d.r * (1 - a));
                        d.g = (uint8_t)(sg * a + d.g * (1 - a));
                        d.b = (uint8_t)(sb * a + d.b * (1 - a));
                        d.a = 255;
                    }
                }
            }
        }
    }
}

} // namespace wf::editor
