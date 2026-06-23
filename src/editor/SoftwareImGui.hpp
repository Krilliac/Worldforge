#pragma once
// ---------------------------------------------------------------------------
// Software ImGui backend: rasterise ImGui's draw lists into a CPU Image -- the
// NullRHI/llvmpipe analog for WorldForge. With rhi_software drawing the 3D scene
// and this drawing the UI chrome, the whole editor composites on the CPU with
// no GPU/display, so a headless run can screenshot the editor to a PNG (and CI
// can verify the UI actually rendered).
//
// Triangles are barycentric-filled with per-vertex colour, the font atlas is
// nearest-sampled for glyphs + the white pixel, clip rects are honoured, and
// results are alpha-blended over the target.
// ---------------------------------------------------------------------------
#include "image.hpp"

struct ImDrawData;

namespace wf::editor {

// Render `drawData` into `target` (RGBA, top-left origin), sampling the font
// atlas (RGBA32, atlasW x atlasH from io.Fonts->GetTexDataAsRGBA32).
void renderImGuiSoftware(const ImDrawData* drawData, Image& target,
                         const unsigned char* atlasPixels, int atlasW, int atlasH);

} // namespace wf::editor
