#pragma once
// ---------------------------------------------------------------------------
// BLP2 texture decoder -> RGBA. Handles the formats vanilla 1.12.1 ships:
//   compression 1 (palettized, alpha depth 0/1/4/8),
//   compression 2 (DXT1 / DXT3 / DXT5 selected by alphaEncoding),
//   compression 3 (raw BGRA8888).
// Layout verified against wowdev.wiki BLP + cmangos/getmangos references.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include "image.hpp"

namespace wf {

struct BlpInfo {
    uint32_t type = 0;
    uint8_t  compression = 0;
    uint8_t  alphaDepth = 0;
    uint8_t  alphaEncoding = 0;
    uint8_t  hasMips = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int      mipCount = 0;   // number of populated mip levels
};

// Decode mip level 0 of a BLP2 buffer to an RGBA Image.
// Throws std::runtime_error on an unsupported/!BLP2 input.
Image decodeBlp(const std::vector<uint8_t>& buf, BlpInfo* outInfo = nullptr);

// Decode a specific mip level (0 = full resolution) to an RGBA Image;
// throws on an out-of-range/empty level or unsupported input.
Image decodeBlpMip(const std::vector<uint8_t>& buf, int mipLevel, BlpInfo* outInfo = nullptr);

// Parse just the BLP header (no pixel decode): dimensions, format, mip count.
// Throws on a non-BLP2 / zero-dimension / JPEG-content (unsupported) input.
BlpInfo readBlpInfo(const std::vector<uint8_t>& buf);

// Choose the mip level best matching a desired on-screen size: the smallest
// (coarsest) mip whose largest dimension is still >= targetMaxDim, so the
// texture is never upsampled from a mip smaller than needed but memory isn't
// wasted on excess resolution. Clamps to [0, mipCount-1]: a target larger than
// the base returns 0 (full res), a target below the coarsest mip returns the
// coarsest. Pure over BlpInfo (no pixel work).
int selectBlpMip(const BlpInfo& info, int targetMaxDim);

// Decode the mip selected by selectBlpMip() for `targetMaxDim`. Convenience over
// readBlpInfo + selectBlpMip + decodeBlpMip for LOD/thumbnail/minimap use.
Image decodeBlpForSize(const std::vector<uint8_t>& buf, int targetMaxDim,
                       BlpInfo* outInfo = nullptr);

} // namespace wf
