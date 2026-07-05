#pragma once
// ---------------------------------------------------------------------------
// Skeletal animation core. Format-agnostic: keyframe tracks (time in ms ->
// value) with linear interpolation for vectors and slerp for quaternions, and a
// bone hierarchy that composes per-bone local transforms (with M2 pivot points)
// into world-space bone matrices. The M2 layer (m2.hpp) feeds these structures
// from a parsed model for a chosen animation.
//
// Bone local transform follows the M2 convention:
//   local = T(pivot) * T(translation) * R(rotation) * S(scale) * T(-pivot)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include "math.hpp"

namespace wf {

inline Vec3 interpolate(const Vec3& a, const Vec3& b, float f) {
    return { a.x + (b.x-a.x)*f, a.y + (b.y-a.y)*f, a.z + (b.z-a.z)*f };
}
inline Quat interpolate(const Quat& a, const Quat& b, float f) { return slerp(a, b, f); }
// Scalar lerp, used by fixed16 material tracks (alpha / texture-weight).
inline float interpolate(float a, float b, float f) { return a + (b - a) * f; }

// M2 track interpolation types (wowdev): 0 = step, 1 = linear, 2 = bezier,
// 3 = hermite. Types 2/3 carry per-key in/out tangents alongside the values.
enum M2Interp : uint16_t {
    M2_INTERP_STEP    = 0,
    M2_INTERP_LINEAR  = 1,
    M2_INTERP_BEZIER  = 2,
    M2_INTERP_HERMITE = 3,
};

// ---- spline (hermite / bezier) key evaluation --------------------------------
// Per-type add/scale/finish so the spline templates run on float, Vec3 and Quat
// alike. Quats are blended per-component and renormalised afterwards (the M2
// convention for spline rotation keys -- vanilla stores rotations as FLOAT
// quaternions, so no fixed-point decode is involved).
inline float splineAdd(float a, float b)             { return a + b; }
inline float splineScale(float v, float s)           { return v * s; }
inline float splineFinish(float v)                   { return v; }
inline Vec3  splineAdd(const Vec3& a, const Vec3& b) { return a + b; }
inline Vec3  splineScale(const Vec3& v, float s)     { return v * s; }
inline Vec3  splineFinish(const Vec3& v)             { return v; }
inline Quat  splineAdd(const Quat& a, const Quat& b) { return { a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w }; }
inline Quat  splineScale(const Quat& q, float s)     { return { q.x*s, q.y*s, q.z*s, q.w*s }; }
inline Quat  splineFinish(const Quat& q)             { return q.normalized(); }

// Cubic hermite between v0 and v1 with v0's out-tangent and v1's in-tangent,
// t normalised to [0,1]. Basis: h1 = 2t^3-3t^2+1, h2 = -2t^3+3t^2,
// h3 = t^3-2t^2+t, h4 = t^3-t^2; result = h1*v0 + h2*v1 + h3*out0 + h4*in1.
template <typename T>
T hermiteSpline(const T& v0, const T& v1, const T& out0, const T& in1, float t) {
    float t2 = t * t, t3 = t2 * t;
    float h1 =  2*t3 - 3*t2 + 1;
    float h2 = -2*t3 + 3*t2;
    float h3 =    t3 - 2*t2 + t;
    float h4 =    t3 -   t2;
    return splineFinish(splineAdd(splineAdd(splineScale(v0, h1), splineScale(v1, h2)),
                                  splineAdd(splineScale(out0, h3), splineScale(in1, h4))));
}

// Cubic bezier reusing the stored tangents as control points offset by
// tangent/3: P0 = v0, P1 = v0 + out0/3, P2 = v1 - in1/3, P3 = v1. This is the
// exact hermite<->bezier conversion, so identical key data samples identically
// under both types.
template <typename T>
T bezierSpline(const T& v0, const T& v1, const T& out0, const T& in1, float t) {
    T p1 = splineAdd(v0, splineScale(out0,  1.0f / 3.0f));
    T p2 = splineAdd(v1, splineScale(in1,  -1.0f / 3.0f));
    float u = 1.0f - t;
    return splineFinish(splineAdd(
        splineAdd(splineScale(v0, u*u*u),   splineScale(p1, 3*u*u*t)),
        splineAdd(splineScale(p2, 3*u*t*t), splineScale(v1, t*t*t))));
}

// Evaluate one segment [k0, k1] at normalised f for any interpolation type.
// Spline types fall back to linear when the caller has no tangents for them.
template <typename T>
T interpolateSegment(uint16_t interp, const T& v0, const T& v1,
                     const T* out0, const T* in1, float f) {
    if (interp == M2_INTERP_HERMITE && out0 && in1) return hermiteSpline(v0, v1, *out0, *in1, f);
    if (interp == M2_INTERP_BEZIER  && out0 && in1) return bezierSpline (v0, v1, *out0, *in1, f);
    return interpolate(v0, v1, f);
}

// A single animation's keyframes for one channel.
template <typename T>
struct KeyTrack {
    uint16_t        interp = 1;     // M2Interp: 0 step, 1 linear, 2 bezier, 3 hermite
    std::vector<uint32_t> times;    // milliseconds, ascending
    std::vector<T>        values;
    // Spline tangents, parallel to `values` when interp is bezier/hermite
    // (empty for step/linear tracks). A 1-key spline track is a constant.
    std::vector<T>        inTan;
    std::vector<T>        outTan;

    bool empty() const { return times.empty(); }

    T sample(uint32_t t, const T& fallback) const {
        if (times.empty())          return fallback;
        if (times.size() == 1)      return values[0];
        if (t <= times.front())     return values.front();
        if (t >= times.back())      return values.back();

        // Find the segment [i, i+1] containing t (linear scan; tracks are short).
        size_t i = 0;
        while (i + 1 < times.size() && times[i + 1] <= t) ++i;
        if (interp == M2_INTERP_STEP) return values[i];    // step
        uint32_t t0 = times[i], t1 = times[i + 1];
        float f = (t1 > t0) ? float(t - t0) / float(t1 - t0) : 0.0f;
        bool tangents = inTan.size() == values.size() && outTan.size() == values.size();
        return interpolateSegment(interp, values[i], values[i + 1],
                                  tangents ? &outTan[i]   : nullptr,
                                  tangents ? &inTan[i + 1] : nullptr, f);
    }
};

// Sample a raw (single-array + per-animation ranges) channel with optional
// global-sequence support -- the on-disk M2Track shape used by bone, color,
// alpha and texture-weight tracks alike. `Channel` must expose `interp`,
// `globalSeq`, `ranges` ([first,last] index pairs), `times` (ms), `values` and
// the spline tangent arrays `inTan`/`outTan` (empty for step/linear tracks).
//
// Behaviour mirrors WoWModelViewer's Animated<T>::getValue:
//   * globalSeq >= 0 : ignore the current animation; sample over the WHOLE
//     track at `globalTimeMs % globalSeqs[globalSeq]` (0 if duration is 0).
//   * otherwise      : slice to ranges[animIndex] (or the whole track if no
//     range is present) and sample at `animTimeMs`.
// Interpolation type 0 = step, 1 = linear (lerp/slerp), 2/3 = bezier/hermite
// (spline via the stored tangents, falling back to linear without them).
template <typename Channel, typename T>
T sampleChannel(const Channel& ch, int animIndex, uint32_t animTimeMs,
                uint32_t globalTimeMs, const std::vector<uint32_t>& globalSeqs,
                const T& fallback) {
    if (ch.times.empty()) return fallback;

    size_t first = 0, last = ch.times.size() - 1;
    uint32_t t = animTimeMs;

    if (ch.globalSeq >= 0) {
        // Global sequence: whole-track sampling on a looped global clock.
        size_t gs = static_cast<size_t>(ch.globalSeq);
        uint32_t dur = (gs < globalSeqs.size()) ? globalSeqs[gs] : 0;
        t = (dur > 0) ? (globalTimeMs % dur) : 0;
    } else if (animIndex >= 0 && static_cast<size_t>(animIndex) < ch.ranges.size()) {
        first = ch.ranges[animIndex].first;
        last  = ch.ranges[animIndex].second;
    }
    if (first >= ch.times.size()) return fallback;
    if (last >= ch.times.size()) last = ch.times.size() - 1;
    if (last < first) return ch.values[first];

    if (first == last)                 return ch.values[first];
    if (t <= ch.times[first])          return ch.values[first];
    if (t >= ch.times[last])           return ch.values[last];

    size_t i = first;
    while (i + 1 <= last && ch.times[i + 1] <= t) ++i;
    if (ch.interp == M2_INTERP_STEP) return ch.values[i];     // step
    uint32_t t0 = ch.times[i], t1 = ch.times[i + 1];
    float f = (t1 > t0) ? float(t - t0) / float(t1 - t0) : 0.0f;
    bool tangents = ch.inTan.size() == ch.values.size() &&
                    ch.outTan.size() == ch.values.size();
    return interpolateSegment(ch.interp, ch.values[i], ch.values[i + 1],
                              tangents ? &ch.outTan[i]   : nullptr,
                              tangents ? &ch.inTan[i + 1] : nullptr, f);
}

// M2 bone flag bits the pose composer consumes (billboards). Other bits exist
// (transformed, kinematic-chain) but are not acted on here.
enum M2BoneFlagBit : uint32_t {
    M2BONE_SPHERICAL_BILLBOARD  = 0x08,  // full camera-facing
    M2BONE_CYL_BILLBOARD_LOCK_X = 0x10,  // rotate about the bone's X axis only
    M2BONE_CYL_BILLBOARD_LOCK_Y = 0x20,  // rotate about the bone's Y axis only
    M2BONE_CYL_BILLBOARD_LOCK_Z = 0x40,  // rotate about the bone's Z axis only
    M2BONE_ANY_BILLBOARD        = 0x78,
};

struct Bone {
    int      parent = -1;       // index into the bone array, -1 for root
    uint32_t flags  = 0;        // M2BoneFlagBit mask (billboards)
    Vec3     pivot;
    KeyTrack<Vec3> translation;
    KeyTrack<Quat> rotation;
    KeyTrack<Vec3> scale;
};

// Compute each bone's local transform matrix at time t (ms).
inline Mat4 boneLocalMatrix(const Bone& b, uint32_t t) {
    Vec3 tr = b.translation.sample(t, Vec3{0, 0, 0});
    Quat ro = b.rotation.sample(t, Quat::identity());
    Vec3 sc = b.scale.sample(t, Vec3{1, 1, 1});
    return Mat4::translate(b.pivot)
         * Mat4::translate(tr)
         * ro.toMat4()
         * Mat4::scale(sc)
         * Mat4::translate(Vec3{-b.pivot.x, -b.pivot.y, -b.pivot.z});
}

// ---- billboard bones ---------------------------------------------------------
// Overwrite the rotation 3x3 of a composed (world/model-space) bone matrix so it
// faces the camera. `modelView` maps model space to eye space (view * model
// placement) and its 3x3 must be a pure rotation; the camera axes expressed in
// model space are therefore its rotation ROWS (the inverse of the model-view
// rotation applied to the eye axes).
//
// Column convention for the rebuilt basis: col0 = right, col1 = up,
// col2 = forward, where forward is the camera's LOOK direction (eye -> pivot
// when the camera looks at the bone) and right = up x forward keeps the basis
// right-handed. Per-axis scale lengths are recovered from the composed columns
// and re-applied; a negative determinant (mirrored bone, e.g. negative scale)
// keeps its mirror by negating the first axis's scale.
//
// Cylindrical variants keep the locked local axis from the animated matrix and
// rebuild the other two by projecting the camera direction onto the plane
// perpendicular to it (degenerate view-along-axis leaves the animated matrix).
inline void applyBoneBillboard(Mat4& g, uint32_t flags, const Mat4& modelView) {
    if (!(flags & M2BONE_ANY_BILLBOARD)) return;

    Vec3 c0{ g.at(0,0), g.at(1,0), g.at(2,0) };
    Vec3 c1{ g.at(0,1), g.at(1,1), g.at(2,1) };
    Vec3 c2{ g.at(0,2), g.at(1,2), g.at(2,2) };
    float det = dot(c0, cross(c1, c2));
    float s[3] = { length(c0), length(c1), length(c2) };
    if (det < 0.0f) s[0] = -s[0];              // preserve a mirrored bone

    // Camera axes in model space = rows of the model-view rotation (its
    // transpose = inverse for a pure rotation). The view looks down -Z.
    Vec3 camUp { modelView.at(1,0), modelView.at(1,1), modelView.at(1,2) };
    Vec3 camFwd{ -modelView.at(2,0), -modelView.at(2,1), -modelView.at(2,2) };

    Vec3 col[3];
    if (flags & M2BONE_SPHERICAL_BILLBOARD) {
        col[1] = camUp;
        col[2] = camFwd;
        col[0] = cross(col[1], col[2]);        // right = up x forward
    } else {
        // Cylindrical: locked axis index k keeps its animated direction; the
        // (k+1)%3 axis aims at the projected camera direction; the (k+2)%3 axis
        // completes the right-handed basis (e_{k+2} = e_k x e_{k+1}).
        int k = (flags & M2BONE_CYL_BILLBOARD_LOCK_X) ? 0
              : (flags & M2BONE_CYL_BILLBOARD_LOCK_Y) ? 1 : 2;
        Vec3 animCol[3] = { c0, c1, c2 };
        Vec3 locked = normalize(animCol[k]);
        Vec3 proj = camFwd - locked * dot(camFwd, locked);
        if (length(proj) < 1e-6f) return;      // camera along the axis: keep animated
        col[k]           = locked;
        col[(k + 1) % 3] = normalize(proj);
        col[(k + 2) % 3] = cross(col[k], col[(k + 1) % 3]);
    }

    for (int c = 0; c < 3; ++c) {
        g.at(0, c) = col[c].x * s[c];
        g.at(1, c) = col[c].y * s[c];
        g.at(2, c) = col[c].z * s[c];
    }
}

// Compose already-evaluated per-bone local matrices into world-space bone
// matrices, honouring hierarchy order (parent may appear after child) via a
// memoised iterative resolve. With `modelView`, billboarded bones are
// re-oriented toward the camera AFTER the parent composition (children inherit
// the billboarded frame).
inline std::vector<Mat4> composePose(const std::vector<Bone>& bones,
                                     const std::vector<Mat4>& locals,
                                     const Mat4* modelView = nullptr) {
    size_t n = bones.size();
    std::vector<Mat4> global(n, Mat4::identity());
    std::vector<char> done(n, 0);

    // Iterative resolve with a visited stack to avoid deep recursion blowups.
    std::vector<int> stack;
    for (size_t i = 0; i < n; ++i) {
        if (done[i]) continue;
        stack.clear();
        int cur = static_cast<int>(i);
        while (cur >= 0 && !done[cur]) { stack.push_back(cur); cur = bones[cur].parent; }
        // Resolve from the top-most unresolved ancestor downwards.
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            int bi = *it;
            int p = bones[bi].parent;
            global[bi] = (p >= 0) ? (global[p] * locals[bi]) : locals[bi];
            if (modelView) applyBoneBillboard(global[bi], bones[bi].flags, *modelView);
            done[bi] = 1;
        }
    }
    return global;
}

// Compose the hierarchy into world-space bone matrices at time t (ms).
inline std::vector<Mat4> computePose(const std::vector<Bone>& bones, uint32_t t) {
    std::vector<Mat4> locals;
    locals.reserve(bones.size());
    for (const Bone& b : bones) locals.push_back(boneLocalMatrix(b, t));
    return composePose(bones, locals);
}

// View-aware pose: like computePose, but bones flagged as billboards face the
// camera. `modelView` = view * model placement (see applyBoneBillboard).
inline std::vector<Mat4> computePose(const std::vector<Bone>& bones, uint32_t t,
                                     const Mat4& modelView) {
    std::vector<Mat4> locals;
    locals.reserve(bones.size());
    for (const Bone& b : bones) locals.push_back(boneLocalMatrix(b, t));
    return composePose(bones, locals, &modelView);
}

} // namespace wf
