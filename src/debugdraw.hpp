#pragma once
// ---------------------------------------------------------------------------
// Debug-draw overlay: an immediate-mode buffer of coloured primitives (lines,
// translucent triangles, points) for visualising things that have no textured
// mesh of their own -- server-side waypoints and nav paths, collision/trigger
// volumes, terrain & doodad wireframe, surface normals, the world grid, a
// camera frustum. Each primitive is tagged with a DebugCategory so the editor
// can toggle layers independently (the "show pathing / colliders / triggers"
// checkboxes).
//
// This is pure geometry generation -> flat vertex lists, so it is fully
// unit-testable headless; the software rasteriser (raster.hpp) draws the lists,
// and a GPU backend would upload them to a Lines/Triangles pipeline. Mirrors
// Spark Engine's DebugVisualizer category/stat model in this project's Z-up math.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>

#include "image.hpp"     // Rgba
#include "math.hpp"
#include "terrain.hpp"   // Mesh / Vertex

namespace wf {

// Layers the editor toggles. Keep Count last.
enum class DebugCategory : uint32_t {
    Generic = 0,
    TerrainWire,   // terrain MCNK mesh edges
    DoodadWire,    // M2 / WMO mesh edges
    Collision,     // collision triangle soup
    Waypoint,      // creature patrol paths
    NavMesh,       // navigation mesh polygons
    NavPath,       // a computed path through the navmesh
    Trigger,       // areatrigger / event volumes
    Marker,        // point markers (spawn points, POIs)
    Normal,        // surface normals
    Grid,          // world reference grid
    Frustum,       // camera frustum
    Count
};

struct DebugVertex {
    Vec3 pos;
    Rgba color;
};

class DebugDraw {
public:
    DebugDraw() { enabled_.fill(true); }

    // ---- primitives -------------------------------------------------------
    void line(const Vec3& a, const Vec3& b, Rgba c, DebugCategory cat = DebugCategory::Generic);
    void triangle(const Vec3& a, const Vec3& b, const Vec3& c, Rgba col,
                  DebugCategory cat = DebugCategory::Collision);
    void point(const Vec3& p, Rgba c, DebugCategory cat = DebugCategory::Marker);

    // ---- shape builders (decompose into the primitives above) -------------
    void aabb(const Vec3& mn, const Vec3& mx, Rgba c, DebugCategory cat = DebugCategory::Trigger);
    void box(const Vec3& center, const Vec3& half, const Quat& rot, Rgba c,
             DebugCategory cat = DebugCategory::Trigger);
    void sphere(const Vec3& center, float radius, Rgba c,
                DebugCategory cat = DebugCategory::Trigger, int segments = 24);
    void circle(const Vec3& center, const Vec3& normal, float radius, Rgba c,
                DebugCategory cat = DebugCategory::Trigger, int segments = 24);
    void cross(const Vec3& p, float size, Rgba c, DebugCategory cat = DebugCategory::Marker);
    void arrow(const Vec3& from, const Vec3& to, Rgba c,
               DebugCategory cat = DebugCategory::NavPath, float headSize = 0.0f);
    // Polyline through pts; with markers, drops a small cross at each node.
    void path(const std::vector<Vec3>& pts, Rgba c,
              DebugCategory cat = DebugCategory::Waypoint, bool markers = true);
    void grid(const Vec3& center, float extent, float step, Rgba minor, Rgba major,
              float majorEvery = 10.0f);
    // Unique edges of a triangle mesh (shared edges drawn once).
    void wireframe(const Mesh& mesh, Rgba c, DebugCategory cat = DebugCategory::TerrainWire);
    void normals(const Mesh& mesh, float length, Rgba c,
                 DebugCategory cat = DebugCategory::Normal);
    // Frustum from an explicit camera basis (avoids needing a matrix inverse).
    void frustum(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
                 double fovYDeg, double aspect, double nearD, double farD, Rgba c,
                 DebugCategory cat = DebugCategory::Frustum);

    void clear();

    // ---- layer toggles ----------------------------------------------------
    void setCategoryEnabled(DebugCategory cat, bool on) { enabled_[idx(cat)] = on; }
    bool categoryEnabled(DebugCategory cat) const { return enabled_[idx(cat)]; }

    // ---- access (only enabled categories) ---------------------------------
    struct Buffers {
        std::vector<DebugVertex> lines;   // 2 verts / segment
        std::vector<DebugVertex> tris;    // 3 verts / triangle
        std::vector<DebugVertex> points;  // 1 vert  / point
    };
    const Buffers& categoryBuffers(DebugCategory cat) const { return cat_[idx(cat)]; }

    struct Stats { int lines = 0; int triangles = 0; int points = 0; };
    Stats stats() const;            // counts enabled categories only

private:
    static size_t idx(DebugCategory c) { return static_cast<size_t>(c); }
    Buffers& bucket(DebugCategory c) { return cat_[idx(c)]; }

    std::array<Buffers, static_cast<size_t>(DebugCategory::Count)> cat_;
    std::array<bool,    static_cast<size_t>(DebugCategory::Count)> enabled_;
};

} // namespace wf
