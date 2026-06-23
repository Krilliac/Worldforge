#pragma once
// ---------------------------------------------------------------------------
// Gizmo / picking math: the backend-agnostic core behind the editor's
// move/rotate/scale handles and viewport object selection. No GL/ImGui here --
// just rays, transforms, and intersection tests, so it is unit-testable
// headless and shared by the software and GPU view paths alike.
//
// Mirrors Spark Engine's GizmoSystem (TRANSLATE/ROTATE/SCALE, world/local,
// snapping) but expressed against this project's Z-up world math.
// ---------------------------------------------------------------------------
#include <cstdint>

#include "math.hpp"
#include "terrain.hpp"   // Mesh, for terrain picking

namespace wf {

enum class GizmoMode  { Translate, Rotate, Scale };
enum class GizmoSpace { World, Local };
enum class Axis       { X, Y, Z };

struct Ray {
    Vec3 origin;
    Vec3 dir;     // expected normalised
};

// A placed object's transform (matches the MDDF/MODF authoring model).
struct Transform {
    Vec3 position;
    Quat rotation = Quat::identity();
    Vec3 scale{1, 1, 1};
};

// Build a world-space pick ray from a camera basis and normalised device
// coords (ndcX,ndcY in [-1,1], +Y up). Uses the same FOV/aspect projection the
// renderer does, so picks line up with what is drawn. forward/right/up must be
// an orthonormal camera basis (forward = look direction).
Ray cameraRay(const Vec3& eye, const Vec3& forward, const Vec3& right,
              const Vec3& up, double fovYDeg, double aspect, float ndcX, float ndcY);

// Ray vs infinite plane (point p0, normal n). On a hit in front of the ray
// (t >= 0) writes t and returns true; returns false if parallel or behind.
bool rayPlane(const Ray& r, const Vec3& p0, const Vec3& n, float& tOut);

// Ray vs triangle (Moeller-Trumbore), single-sided off. Writes t on hit.
bool rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c, float& tOut);

// Nearest triangle hit against a mesh; writes the world-space hit point.
bool pickMesh(const Ray& r, const Mesh& mesh, Vec3& hitOut);

// Snap helpers (no-op when step <= 0).
float snap(float v, float step);
Vec3  snap(const Vec3& v, float step);

// Translate `pos` along a world `axis` by the pointer motion between two pick
// rays, measured as displacement along that axis (the classic axis-handle drag).
Vec3 dragAlongAxis(const Vec3& pos, const Vec3& axis, const Ray& from, const Ray& to);

} // namespace wf
