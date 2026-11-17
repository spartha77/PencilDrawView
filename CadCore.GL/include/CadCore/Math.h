#pragma once
// =============================================================================
// CadCore.GL / Math.h
//
// Small, dependency-free vector / matrix types (no GLM needed).
//
// CONVENTIONS USED THROUGHOUT THE ENGINE
//   * Units        : millimetres.
//   * World frame  : right-handed, Z is UP, the reference grid lies in XY.
//   * Matrices     : COLUMN-MAJOR, exactly what OpenGL / GLSL expects.
//                    Element (row r, column c) is stored at m[c*4 + r].
//                    A point is transformed as  p' = M * p.
// =============================================================================

#include <algorithm>
#include <cmath>

namespace CadCore {

constexpr float kPi = 3.14159265358979323846f;
inline float DegToRad(float d) { return d * kPi / 180.0f; }
inline float RadToDeg(float r) { return r * 180.0f / kPi; }

// -----------------------------------------------------------------------------
// Vec3
// -----------------------------------------------------------------------------
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
/// Returns the unit vector, or `fallback` when the input is (nearly) zero.
inline Vec3 Normalize(const Vec3& v, const Vec3& fallback = Vec3(0, 0, 1)) {
    float len = Length(v);
    return len > 1e-12f ? v * (1.0f / len) : fallback;
}

// -----------------------------------------------------------------------------
// Mat4 (column-major)
// -----------------------------------------------------------------------------
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 Identity() { return Mat4(); }

    static Mat4 Translation(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static Mat4 Scale(float s) {
        Mat4 r;
        r.m[0] = s; r.m[5] = s; r.m[10] = s;
        return r;
    }

    /// Builds a rotation whose columns are the images of the X, Y and Z axes.
    static Mat4 FromBasis(const Vec3& xAxis, const Vec3& yAxis, const Vec3& zAxis) {
        Mat4 r;
        r.m[0] = xAxis.x; r.m[1] = xAxis.y; r.m[2]  = xAxis.z;
        r.m[4] = yAxis.x; r.m[5] = yAxis.y; r.m[6]  = yAxis.z;
        r.m[8] = zAxis.x; r.m[9] = zAxis.y; r.m[10] = zAxis.z;
        return r;
    }

    /// Orthographic projection (same as glOrtho).
    static Mat4 Ortho(float l, float r, float b, float t, float n, float f) {
        Mat4 o;
        o.m[0]  = 2.0f / (r - l);
        o.m[5]  = 2.0f / (t - b);
        o.m[10] = -2.0f / (f - n);
        o.m[12] = -(r + l) / (r - l);
        o.m[13] = -(t + b) / (t - b);
        o.m[14] = -(f + n) / (f - n);
        return o;
    }

    /// General (possibly off-centre) perspective frustum (same as glFrustum).
    /// The off-centre form is what makes the 1- and 2-point perspective
    /// "shift lens" camera possible - see Camera.h.
    static Mat4 Frustum(float l, float r, float b, float t, float n, float f) {
        Mat4 o;
        o.m[0]  = 2.0f * n / (r - l);
        o.m[5]  = 2.0f * n / (t - b);
        o.m[8]  = (r + l) / (r - l);
        o.m[9]  = (t + b) / (t - b);
        o.m[10] = -(f + n) / (f - n);
        o.m[11] = -1.0f;
        o.m[14] = -2.0f * f * n / (f - n);
        o.m[15] = 0.0f;
        return o;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }

    /// Transforms a point (w = 1), ignoring any projective row.
    Vec3 TransformPoint(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
};

/// General 4x4 matrix inverse (cofactor expansion). Returns false when the
/// matrix cannot be inverted. Used for picking (screen pixel -> world ray).
inline bool Invert(const Mat4& in, Mat4& out) {
    const float* m = in.m;
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    const float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-30f) return false;
    const float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * invDet;
    return true;
}

/// Writes the 3x3 "normal matrix" for a model matrix into out[9] (column-major).
/// We use the cofactor matrix, which equals det(M) * inverse(M)^T. The scale
/// factor does not matter because the shader re-normalises the normal, and
/// unlike a true inverse it never divides by zero.
inline void NormalMatrix(const Mat4& model, float out[9]) {
    const float* a = model.m;
    const float a00 = a[0], a10 = a[1], a20 = a[2];
    const float a01 = a[4], a11 = a[5], a21 = a[6];
    const float a02 = a[8], a12 = a[9], a22 = a[10];
    out[0] = a11 * a22 - a12 * a21;   // column 0
    out[1] = a02 * a21 - a01 * a22;
    out[2] = a01 * a12 - a02 * a11;
    out[3] = a12 * a20 - a10 * a22;   // column 1
    out[4] = a00 * a22 - a02 * a20;
    out[5] = a02 * a10 - a00 * a12;
    out[6] = a10 * a21 - a11 * a20;   // column 2
    out[7] = a01 * a20 - a00 * a21;
    out[8] = a00 * a11 - a01 * a10;
}

// -----------------------------------------------------------------------------
// Axis-aligned bounding box
// -----------------------------------------------------------------------------
struct AABB {
    Vec3 min{ 1e30f,  1e30f,  1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void Expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    void Expand(const AABB& b) {
        if (!b.IsValid()) return;
        Expand(b.min);
        Expand(b.max);
    }
    Vec3  Center() const { return (min + max) * 0.5f; }
    float Radius() const { return Length(max - min) * 0.5f; }

    /// Bounding box of this box after an affine transform (all 8 corners).
    AABB Transformed(const Mat4& xf) const {
        AABB r;
        if (!IsValid()) return r;
        for (int i = 0; i < 8; ++i)
            r.Expand(xf.TransformPoint({(i & 1) ? max.x : min.x,
                                        (i & 2) ? max.y : min.y,
                                        (i & 4) ? max.z : min.z}));
        return r;
    }
};

} // namespace CadCore
