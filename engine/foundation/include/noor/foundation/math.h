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

} // namespace noor::math
