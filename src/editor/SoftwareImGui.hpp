#pragma once
// ---------------------------------------------------------------------------
// Software ImGui backend: rasterise ImGui's draw lists into a CPU Image -- the
// NullRHI/llvmpipe analog for WorldForge. With rhi_software drawing the 3D scene
// and this drawing the UI chrome, the whole editor composites on the CPU with
// no GPU/display, so a headless run can screenshot the editor to a PNG.
//
// Triangles are barycentric-filled with per-vertex colour, textures are
// nearest-sampled (the font atlas for glyphs/white pixel, plus any registered
// image such as the embedded 3D viewport), clip rects are honoured, and results
// are alpha-blended over the target.
// ---------------------------------------------------------------------------
#include <unordered_map>

#include "image.hpp"
#include "imgui.h"   // ImDrawData / ImTextureID

namespace wf::editor {

struct SoftTexture {
    const unsigned char* pixels = nullptr;   // RGBA8
    int w = 0, h = 0;
};

// Atlas-only convenience (back-compat): every command samples `atlasPixels`.
void renderImGuiSoftware(const ImDrawData* drawData, Image& target,
                         const unsigned char* atlasPixels, int atlasW, int atlasH);

// Multi-texture renderer: register the font atlas + any embedded images (e.g.
// the viewport scene) under their ImTextureID, then render -- each draw command
// samples the texture ImGui tagged it with.
class SoftwareImGuiRenderer {
public:
    void setTexture(ImTextureID id, const SoftTexture& t) { textures_[id] = t; }
    void render(const ImDrawData* drawData, Image& target) const;
private:
    std::unordered_map<ImTextureID, SoftTexture> textures_;
};

} // namespace wf::editor
