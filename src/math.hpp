#pragma once
// ---------------------------------------------------------------------------
// Minimal linear algebra for the engine. Column-major 4x4 matrices (OpenGL
// convention): a transform is applied as m * v, and composition reads
// right-to-left, i.e. (A * B) applies B first. Kept dependency-free and
// header-only so every module can use it; correctness is unit-tested.
// ---------------------------------------------------------------------------
#include <array>
#include <cmath>
#include <cstddef>

namespace wf {

constexpr double kPi = 3.14159265358979323846;
inline double radians(double deg) { return deg * (kPi / 180.0); }

struct Vec2 { float x = 0, y = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3  operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3  operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3  operator*(float s)       const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3  cross(const Vec3& a, const Vec3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3  normalize(const Vec3& v) {
    float len = length(v);
    return len > 1e-8f ? v * (1.0f / len) : v;
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

// Column-major 4x4. m[col*4 + row]; element (row r, col c) is m[c*4 + r].
struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    float& at(int row, int col)             { return m[col * 4 + row]; }
    float  at(int row, int col) const       { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r; // r = this * o
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += at(row, k) * o.at(k, c);
                r.at(row, c) = sum;
            }
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        return {
            at(0,0)*v.x + at(0,1)*v.y + at(0,2)*v.z + at(0,3)*v.w,
            at(1,0)*v.x + at(1,1)*v.y + at(1,2)*v.z + at(1,3)*v.w,
            at(2,0)*v.x + at(2,1)*v.y + at(2,2)*v.z + at(2,3)*v.w,
            at(3,0)*v.x + at(3,1)*v.y + at(3,2)*v.z + at(3,3)*v.w,
        };
    }

    static Mat4 translate(const Vec3& t) {
        Mat4 r = identity();
        r.at(0,3) = t.x; r.at(1,3) = t.y; r.at(2,3) = t.z;
        return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r = identity();
        r.at(0,0) = s.x; r.at(1,1) = s.y; r.at(2,2) = s.z;
        return r;
    }

    // Rotation about a normalized axis by `deg` degrees (right-handed).
    static Mat4 rotateAxis(const Vec3& axisIn, double deg) {
        Vec3 a = normalize(axisIn);
        float c = static_cast<float>(std::cos(radians(deg)));
        float s = static_cast<float>(std::sin(radians(deg)));
        float t = 1.0f - c;
        Mat4 r = identity();
        r.at(0,0) = t*a.x*a.x + c;      r.at(0,1) = t*a.x*a.y - s*a.z;  r.at(0,2) = t*a.x*a.z + s*a.y;
        r.at(1,0) = t*a.x*a.y + s*a.z;  r.at(1,1) = t*a.y*a.y + c;      r.at(1,2) = t*a.y*a.z - s*a.x;
        r.at(2,0) = t*a.x*a.z - s*a.y;  r.at(2,1) = t*a.y*a.z + s*a.x;  r.at(2,2) = t*a.z*a.z + c;
        return r;
    }
    static Mat4 rotateX(double d) { return rotateAxis({1,0,0}, d); }
    static Mat4 rotateY(double d) { return rotateAxis({0,1,0}, d); }
    static Mat4 rotateZ(double d) { return rotateAxis({0,0,1}, d); }

    // Right-handed look-at (camera looks down -Z in eye space), OpenGL-style.
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.at(0,0)=s.x; r.at(0,1)=s.y; r.at(0,2)=s.z;
        r.at(1,0)=u.x; r.at(1,1)=u.y; r.at(1,2)=u.z;
        r.at(2,0)=-f.x; r.at(2,1)=-f.y; r.at(2,2)=-f.z;
        r.at(0,3)=-dot(s, eye);
        r.at(1,3)=-dot(u, eye);
        r.at(2,3)= dot(f, eye);
        return r;
    }

    // Right-handed perspective, depth mapped to [-1, 1] (OpenGL clip space).
    static Mat4 perspective(double fovYDeg, double aspect, double zNear, double zFar) {
        float f = static_cast<float>(1.0 / std::tan(radians(fovYDeg) / 2.0));
        Mat4 r; // zero-initialised
        r.at(0,0) = static_cast<float>(f / aspect);
        r.at(1,1) = f;
        r.at(2,2) = static_cast<float>((zFar + zNear) / (zNear - zFar));
        r.at(2,3) = static_cast<float>((2.0 * zFar * zNear) / (zNear - zFar));
        r.at(3,2) = -1.0f;
        return r;
    }
};

// Quaternion (x, y, z, w). Used for M2 bone rotation tracks.
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return {0, 0, 0, 1}; }

    float length() const { return std::sqrt(x*x + y*y + z*z + w*w); }
    Quat normalized() const {
        float l = length();
        if (l < 1e-8f) return identity();
        return { x/l, y/l, z/l, w/l };
    }

    Mat4 toMat4() const {
        Quat q = normalized();
        float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
        float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
        float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
        Mat4 m = Mat4::identity();
        m.at(0,0) = 1 - 2*(yy+zz); m.at(0,1) = 2*(xy-wz);     m.at(0,2) = 2*(xz+wy);
        m.at(1,0) = 2*(xy+wz);     m.at(1,1) = 1 - 2*(xx+zz); m.at(1,2) = 2*(yz-wx);
        m.at(2,0) = 2*(xz-wy);     m.at(2,1) = 2*(yz+wx);     m.at(2,2) = 1 - 2*(xx+yy);
        return m;
    }
};

inline float dot(const Quat& a, const Quat& b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

// Spherical linear interpolation, shortest-arc, with a lerp fallback for nearly
// parallel quaternions.
inline Quat slerp(const Quat& a, Quat b, float t) {
    float d = dot(a, b);
    if (d < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }   // shortest path
    if (d > 0.9995f) {
        Quat r{ a.x + t*(b.x-a.x), a.y + t*(b.y-a.y),
                a.z + t*(b.z-a.z), a.w + t*(b.w-a.w) };
        return r.normalized();
    }
    float theta0 = std::acos(d);
    float theta  = theta0 * t;
    float sin0   = std::sin(theta0);
    float s0 = std::sin(theta0 - theta) / sin0;
    float s1 = std::sin(theta) / sin0;
    return { s0*a.x + s1*b.x, s0*a.y + s1*b.y, s0*a.z + s1*b.z, s0*a.w + s1*b.w };
}

} // namespace wf
