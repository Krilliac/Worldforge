#pragma once
// ---------------------------------------------------------------------------
// Vanilla WoW coordinate system. Verified against wowdev.wiki ADT/v18.
//
// World space (terrain + ALL network packets): right-handed, Z-up.
//   +X = north, +Y = west, +Z = height (0 = sea level). Origin at map centre.
//   Map north-west corner = (X, Y) = (+32T, +32T) = (+17066.67, +17066.67).
//   Tile (block) index for an axis value:  floor(32 - axis / TILE_SIZE).
//
// MDDF/MODF placement space differs from world space (this is the classic
// "everything is mirrored" trap). The wiki coordinate table gives:
//   placement = Left*x' + Up*y + Forward*z',  x' = 32T - sx,  z' = 32T - sz
// In world space (Forward=+X north, Left=+Y west, Up=+Z) this resolves to:
//   worldX(north) = 32T - sz
//   worldY(west)  = 32T - sx
//   worldZ(up)    = sy
// Implemented + round-trip unit-tested in tests/.
// ---------------------------------------------------------------------------
#include <cmath>
#include "math.hpp"

namespace wf {

constexpr double TILE_SIZE  = 533.0 + 1.0 / 3.0;  // 533.33333 yds / ADT tile
constexpr double CHUNK_SIZE = TILE_SIZE / 16.0;   // 33.33333 yds  / MCNK
constexpr double UNIT_SIZE  = CHUNK_SIZE / 8.0;   // 4.16666 yds   / cell
constexpr double ZEROPOINT  = 32.0 * TILE_SIZE;   // 17066.66656 origin offset

// Block (tile) index for a world-space axis value. wowdev.wiki formula.
inline int tileIndexFromCoord(double axis) {
    return static_cast<int>(std::floor(32.0 - (axis / TILE_SIZE)));
}

// MDDF/MODF stored placement position -> world space (north/west/up).
inline Vec3 placementToWorld(const Vec3& stored) {
    return {
        static_cast<float>(ZEROPOINT) - stored.z, // X north
        static_cast<float>(ZEROPOINT) - stored.x, // Y west
        stored.y                                  // Z up
    };
}

// Inverse: world space -> stored placement position (for authoring/round-trip).
inline Vec3 worldToPlacement(const Vec3& world) {
    return {
        static_cast<float>(ZEROPOINT) - world.y, // sx
        world.z,                                 // sy
        static_cast<float>(ZEROPOINT) - world.x  // sz
    };
}

// World-space NW corner (max X north, max Y west) of an MCNK inside an ADT tile.
// blockX/blockY are the ADT *filename* indices ("Map_blockX_blockY.adt").
//
// VERIFIED against real MCNK header positions (offset 0x68) in Azeroth_32_48:
// the filename's FIRST index (blockX) is the WEST tile index and the SECOND
// (blockY) is the NORTH tile index -- the two are NOT symmetric, so they must
// not be transposed:
//   worldX(north) = (32 - blockY) * TILE - row * CHUNK
//   worldY(west)  = (32 - blockX) * TILE - col * CHUNK
//   row (north-south, 0 = north edge) advances southward -> decreasing X  (= MCNK IndexY)
//   col (west-east,   0 = west  edge) advances eastward  -> decreasing Y  (= MCNK IndexX)
// e.g. tile 32,48 chunk Index(0,0): (32-48)*T = -8533.33 north, (32-32)*T = 0 west,
// which reproduces the header's stored position (-8533.334, 0.000) exactly.
inline Vec3 chunkCornerWorld(int blockX, int blockY, int row, int col, float baseHeight) {
    double x = (32.0 - blockY) * TILE_SIZE - row * CHUNK_SIZE; // north
    double y = (32.0 - blockX) * TILE_SIZE - col * CHUNK_SIZE; // west
    return { static_cast<float>(x), static_cast<float>(y), baseHeight };
}

} // namespace wf
