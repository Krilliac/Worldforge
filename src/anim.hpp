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

// A single animation's keyframes for one channel.
template <typename T>
struct KeyTrack {
    uint16_t        interp = 1;     // 0 = none/step, 1 = linear (slerp for Quat)
    std::vector<uint32_t> times;    // milliseconds, ascending
    std::vector<T>        values;

    bool empty() const { return times.empty(); }

    T sample(uint32_t t, const T& fallback) const {
        if (times.empty())          return fallback;
        if (times.size() == 1)      return values[0];
        if (t <= times.front())     return values.front();
        if (t >= times.back())      return values.back();

        // Find the segment [i, i+1] containing t (linear scan; tracks are short).
        size_t i = 0;
        while (i + 1 < times.size() && times[i + 1] <= t) ++i;
        if (interp == 0) return values[i];                 // step
        uint32_t t0 = times[i], t1 = times[i + 1];
        float f = (t1 > t0) ? float(t - t0) / float(t1 - t0) : 0.0f;
        return interpolate(values[i], values[i + 1], f);
    }
};

struct Bone {
    int  parent = -1;           // index into the bone array, -1 for root
    Vec3 pivot;
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

// Compose the hierarchy into world-space bone matrices. Handles arbitrary bone
// ordering (parent may appear after child) via memoised recursion.
inline std::vector<Mat4> computePose(const std::vector<Bone>& bones, uint32_t t) {
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
            Mat4 local = boneLocalMatrix(bones[bi], t);
            int p = bones[bi].parent;
            global[bi] = (p >= 0) ? (global[p] * local) : local;
            done[bi] = 1;
        }
    }
    return global;
}

} // namespace wf
