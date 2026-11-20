#pragma once
// Minimal linear algebra for the engine. Conventions (ADR-022): right-handed, +Y up,
// −Z forward, meters, radians, column vectors, column-major storage, reverse-Z clip.

#include <cmath>
#include <numbers>

namespace noor::math {

inline constexpr float kPi = std::numbers::pi_v<float>;

[[nodiscard]] constexpr float radians(float degrees) noexcept { return degrees * (kPi / 180.0f); }

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3 operator+(Vec3 o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr bool operator==(const Vec3&) const = default;
};

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr bool operator==(const Vec4&) const = default;
};

[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline float length(Vec3 v) noexcept { return std::sqrt(dot(v, v)); }
[[nodiscard]] inline Vec3 normalize(Vec3 v) noexcept {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : Vec3{};
}

// 4x4 matrix, column-major: m[c] is column c. Element (row r, column c) is m[c][r].
struct Mat4 {
    Vec4 m[4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return {}; }

    [[nodiscard]] constexpr float at(int row, int col) const noexcept {
        const Vec4& c = m[col];
        return row == 0 ? c.x : row == 1 ? c.y : row == 2 ? c.z : c.w;
    }
    constexpr void set(int row, int col, float v) noexcept {
        Vec4& c = m[col];
        (row == 0 ? c.x : row == 1 ? c.y : row == 2 ? c.z : c.w) = v;
    }

    [[nodiscard]] constexpr Mat4 operator*(const Mat4& b) const noexcept {
        Mat4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += at(row, k) * b.at(k, col);
                r.set(row, col, sum);
            }
        }
        return r;
    }

    [[nodiscard]] constexpr Vec4 operator*(Vec4 v) const noexcept {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }

    [[nodiscard]] constexpr Vec3 transform_point(Vec3 p) const noexcept {
        const Vec4 r = *this * Vec4{p.x, p.y, p.z, 1.0f};
        return {r.x / r.w, r.y / r.w, r.z / r.w};
    }
};

[[nodiscard]] constexpr Mat4 translation(Vec3 t) noexcept {
    Mat4 r;
    r.m[3] = {t.x, t.y, t.z, 1};
    return r;
}

[[nodiscard]] constexpr Mat4 scale(Vec3 s) noexcept {
    Mat4 r;
    r.m[0].x = s.x;
    r.m[1].y = s.y;
    r.m[2].z = s.z;
    return r;
}

// Rotation by `angle` radians around a unit axis, counter-clockwise when looking down the axis
// toward the origin (right-hand rule).
[[nodiscard]] inline Mat4 rotation(Vec3 axis, float angle) noexcept {
    const Vec3 a = normalize(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    Mat4 r;
    r.m[0] = {t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y, 0};
    r.m[1] = {t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x, 0};
    r.m[2] = {t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c, 0};
    return r;
}

// Unit quaternion rotation, stored [x, y, z, w] like glTF.
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    [[nodiscard]] static Quat from_axis_angle(Vec3 axis, float angle) noexcept {
        const Vec3 a = normalize(axis);
        const float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }
    // Hamilton product: (a * b) applies b first, then a.
    [[nodiscard]] constexpr Quat operator*(const Quat& b) const noexcept {
        return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z};
    }
    constexpr bool operator==(const Quat&) const = default;
};

[[nodiscard]] inline Mat4 to_mat4(Quat q) noexcept {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 0.0f) {
        q = {q.x / len, q.y / len, q.z / len, q.w / len};
    }
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 r;
    r.m[0] = {1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0};
    r.m[1] = {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0};
    r.m[2] = {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0};
    return r;
}

// Translation * Rotation * Scale (ADR-022 transform order).
[[nodiscard]] inline Mat4 compose(Vec3 t, Quat r, Vec3 s) noexcept {
    return translation(t) * to_mat4(r) * scale(s);
}

// View matrix for a camera at `eye` looking at `target` (right-handed: camera looks down −Z).
[[nodiscard]] inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0] = {s.x, u.x, -f.x, 0};
    r.m[1] = {s.y, u.y, -f.y, 0};
    r.m[2] = {s.z, u.z, -f.z, 0};
    r.m[3] = {-dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    return r;
}

// Vulkan perspective projection: reverse-Z, infinite far plane, Y flipped so +Y is up on
// screen. Points at distance `near_plane` map to depth 1, infinitely far points to depth 0.
[[nodiscard]] inline Mat4 perspective_reverse_z(float fov_y, float aspect, float near_plane) noexcept {
    const float f = 1.0f / std::tan(fov_y * 0.5f);
    Mat4 r;
    r.m[0] = {f / aspect, 0, 0, 0};
    r.m[1] = {0, -f, 0, 0};
    r.m[2] = {0, 0, 0, -1};
    r.m[3] = {0, 0, near_plane, 0};
    return r;
}

// Vulkan orthographic projection (Milestone 9A, part 1): reverse-Z like the perspective one, Y flipped. `height` is the
// whole height the view shows (metres); depth 1 at `near_plane`, 0 at `far_plane` (finite: parallel views need one).
[[nodiscard]] inline Mat4 orthographic_reverse_z(float height, float aspect, float near_plane, float far_plane) noexcept {
    const float hh = 0.5f * height, range = far_plane - near_plane;
    Mat4 r;
    r.m[0] = {1.0f / (hh * aspect), 0, 0, 0};
    r.m[1] = {0, -1.0f / hh, 0, 0};
    r.m[2] = {0, 0, 1.0f / range, 0};
    r.m[3] = {0, 0, far_plane / range, 1};
    return r;
}

// Direction transform: the 3x3 part only (no translation).
[[nodiscard]] constexpr Vec3 transform_vector(const Mat4& m, Vec3 v) noexcept {
    return {m.at(0, 0) * v.x + m.at(0, 1) * v.y + m.at(0, 2) * v.z, m.at(1, 0) * v.x + m.at(1, 1) * v.y + m.at(1, 2) * v.z,
            m.at(2, 0) * v.x + m.at(2, 1) * v.y + m.at(2, 2) * v.z};
}

// General 4x4 inverse (cofactors). Returns identity for a singular matrix.
[[nodiscard]] inline Mat4 inverse(const Mat4& a) noexcept {
    float m[16], inv[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) m[c * 4 + r] = a.at(r, c);
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return Mat4::identity();
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) r.set(row, c, inv[c * 4 + row] / det);
    return r;
}

// Inverse of a unit quaternion.
[[nodiscard]] constexpr Quat conjugate(Quat q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }

[[nodiscard]] inline Quat normalize_quat(Quat q) noexcept {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > 0.0f ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}

// Blend between two rotations (t = 0: a, 1: b) the short way round; normalised lerp, which is close
// enough to slerp for the small steps it's used for (interpolating between fixed steps).
[[nodiscard]] inline Quat nlerp(Quat a, Quat b, float t) noexcept {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0 ? -1.0f : 1.0f;
    return normalize_quat({a.x + (b.x * s - a.x) * t, a.y + (b.y * s - a.y) * t, a.z + (b.z * s - a.z) * t, a.w + (b.w * s - a.w) * t});
}

// Rotates v by the unit quaternion q.
[[nodiscard]] inline Vec3 rotate(Quat q, Vec3 v) noexcept {
    const Quat p = q * Quat{v.x, v.y, v.z, 0} * conjugate(q);
    return {p.x, p.y, p.z};
}

} // namespace noor::math
