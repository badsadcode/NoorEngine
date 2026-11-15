#include "viewport_tools.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace noor::editor {

using math::Vec3;
using math::cross;
using math::dot;
using math::normalize;

Ray screen_ray(const CameraBasis& c, float width, float height, float px, float py) {
    const float aspect = height > 0 ? width / height : 1.0f;
    const float t = std::tan(c.fov_y * 0.5f);
    const float x = (2.0f * (px + 0.5f) / std::max(width, 1.0f) - 1.0f) * t * aspect;
    const float y = (1.0f - 2.0f * (py + 0.5f) / std::max(height, 1.0f)) * t;
    return {c.eye, normalize(c.forward + c.right * x + c.up * y)};
}

std::optional<ScreenPoint> world_to_screen(const CameraBasis& c, float width, float height, Vec3 p) {
    const Vec3 v = p - c.eye;
    const float z = dot(v, c.forward);
    if (z <= 1e-6f || width <= 0 || height <= 0) return std::nullopt;
    const float t = std::tan(c.fov_y * 0.5f);
    const float x = dot(v, c.right) / (z * t * (width / height));
    const float y = dot(v, c.up) / (z * t);
    return ScreenPoint{(x + 1.0f) * 0.5f * width - 0.5f, (1.0f - y) * 0.5f * height - 0.5f, math::length(v)};
}

Ray to_object_space(const Ray& r, const math::Mat4& world) {
    const math::Mat4 inv = math::inverse(world);
    return {inv.transform_point(r.origin), math::transform_vector(inv, r.dir)};
}

std::optional<float> ray_triangle(const Ray& r, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(r.dir, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f) return std::nullopt;
    const float inv = 1.0f / det;
    const Vec3 s = r.origin - a;
    const float u = dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    const Vec3 q = cross(s, e1);
    const float v = dot(r.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;
    const float t = dot(e2, q) * inv;
    return t > 1e-6f ? std::optional(t) : std::nullopt;
}

std::optional<float> ray_aabb(const Ray& r, Vec3 lo, Vec3 hi) {
    float t0 = 0.0f, t1 = std::numeric_limits<float>::max();
    const float o[3] = {r.origin.x, r.origin.y, r.origin.z}, d[3] = {r.dir.x, r.dir.y, r.dir.z};
    const float l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-12f) {
            if (o[i] < l[i] || o[i] > h[i]) return std::nullopt;
            continue;
        }
        float a = (l[i] - o[i]) / d[i], b = (h[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return std::nullopt;
    }
    return t0;
}

std::optional<float> ray_plane(const Ray& r, Vec3 point, Vec3 normal) {
    const float denom = dot(r.dir, normal);
    if (std::fabs(denom) < 1e-8f) return std::nullopt;
    const float t = dot(point - r.origin, normal) / denom;
    return t > 0.0f ? std::optional(t) : std::nullopt;
}

// ---- EditorCamera ----

namespace {
constexpr float kMaxPitch = 1.55f;

Vec3 direction(float yaw, float pitch) {
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}
} // namespace

Vec3 EditorCamera::eye() const { return target + direction(yaw, pitch) * distance; }

math::Mat4 EditorCamera::view() const { return math::look_at(eye(), target, {0, 1, 0}); }

CameraBasis EditorCamera::basis() const {
    const Vec3 e = eye();
    const Vec3 f = normalize(target - e);
    const Vec3 r = normalize(cross(f, {0, 1, 0}));
    return {e, f, r, cross(r, f), fov_y};
}

void EditorCamera::frame(Vec3 lo, Vec3 hi) {
    target = (lo + hi) * 0.5f;
    const float radius = std::max(1.0f, math::length(hi - lo) * 0.5f);
    distance = radius / std::sin(fov_y * 0.5f) * 1.1f;
}

void EditorCamera::orbit(float dx, float dy) {
    yaw -= dx * turn_speed;
    pitch = std::clamp(pitch + dy * turn_speed, -kMaxPitch, kMaxPitch);
}

void EditorCamera::look(float dx, float dy) {
    const Vec3 e = eye();
    orbit(dx, dy);  // same angles; then move the target so the eye stays put
    target = e - direction(yaw, pitch) * distance;
}

void EditorCamera::pan(float dx, float dy) {
    const auto b = basis();
    const float k = distance * 0.0015f;
    target = target - b.right * (dx * k) + b.up * (dy * k);
}

void EditorCamera::zoom(float notches) { distance = std::clamp(distance * std::pow(0.88f, notches), 0.5f, 50'000.0f); }

void EditorCamera::fly(Vec3 move, float seconds, bool fast) {
    if (move == Vec3{}) return;
    const auto b = basis();
    const float step = fly_speed * (fast ? 4.0f : 1.0f) * std::clamp(seconds, 0.0f, 0.1f);  // no leaps after a stall
    target = target + (b.right * move.x + Vec3{0, 1, 0} * move.y + b.forward * move.z) * step;
}

void EditorCamera::change_fly_speed(float notches) { fly_speed = std::clamp(fly_speed * std::pow(1.25f, notches), 0.25f, 2000.0f); }

// ---- Gizmos ----

int axis_index(Handle h) {
    switch (h) {
    case Handle::MoveX:
    case Handle::RotateX:
    case Handle::ScaleX: return 0;
    case Handle::MoveY:
    case Handle::RotateY:
    case Handle::ScaleY: return 1;
    case Handle::MoveZ:
    case Handle::RotateZ:
    case Handle::ScaleZ: return 2;
    default: return -1;
    }
}

Vec3 handle_axis(Handle h) {
    switch (axis_index(h)) {
    case 0: return {1, 0, 0};
    case 2: return {0, 0, 1};
    default: return {0, 1, 0};
    }
}

bool is_rotate(Handle h) { return h == Handle::RotateX || h == Handle::RotateY || h == Handle::RotateZ; }
bool is_scale(Handle h) { return h == Handle::ScaleX || h == Handle::ScaleY || h == Handle::ScaleZ || h == Handle::ScaleAll; }

float snap(float v, float step) { return step > 0.0f ? std::round(v / step) * step : v; }

namespace {

// Squared distance between a (unit-direction) ray and a segment, and the ray parameter there.
std::pair<float, float> ray_segment(const Ray& r, Vec3 p0, Vec3 p1) {
    const Vec3 u = r.dir, v = p1 - p0, w = r.origin - p0;
    const float a = dot(u, u), b = dot(u, v), c = dot(v, v), d = dot(u, w), e = dot(v, w);
    const float den = a * c - b * b;
    float s = den > 1e-12f ? (b * e - c * d) / den : 0.0f;  // ray parameter
    float t = den > 1e-12f ? (a * e - b * d) / den : e / c;  // segment parameter
    t = std::clamp(t, 0.0f, 1.0f);
    s = std::max(0.0f, (b * t - d) / a);
    const Vec3 diff = (r.origin + u * s) - (p0 + v * t);
    return {dot(diff, diff), s};
}

// Two unit vectors spanning the plane perpendicular to `n`.
std::pair<Vec3, Vec3> plane_basis(Vec3 n) {
    const Vec3 helper = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 a = normalize(cross(helper, n));
    return {a, cross(n, a)};
}

} // namespace

Handle pick_handle(const Ray& ray, GizmoMode mode, Vec3 center, float size, math::Quat orientation) {
    Handle best = Handle::None;
    float best_t = std::numeric_limits<float>::max();
    const float tolerance = size * 0.09f;
    auto consider = [&](Handle h, float dist2, float t) {
        if (dist2 <= tolerance * tolerance && t < best_t) {
            best = h;
            best_t = t;
        }
    };

    if (mode == GizmoMode::Move) {
        for (Handle h : {Handle::MoveX, Handle::MoveY, Handle::MoveZ}) {
            const auto [d2, t] = ray_segment(ray, center, center + handle_axis(h) * size);
            consider(h, d2, t);
        }
        if (auto t = ray_plane(ray, center, {0, 1, 0})) {
            const Vec3 p = ray.origin + ray.dir * *t - center;
            if (p.x >= kPlaneMin * size && p.x <= kPlaneMax * size && p.z >= kPlaneMin * size && p.z <= kPlaneMax * size &&
                *t < best_t) {
                best = Handle::MoveXZ;
                best_t = *t;
            }
        }
    } else if (mode == GizmoMode::Scale) {
        for (Handle h : {Handle::ScaleX, Handle::ScaleY, Handle::ScaleZ}) {
            const auto [d2, t] = ray_segment(ray, center, center + math::rotate(orientation, handle_axis(h)) * size);
            consider(h, d2, t);
        }
        // The centre cube wins near the middle, where all three axes meet.
        const float s = std::max(0.0f, dot(center - ray.origin, ray.dir) / dot(ray.dir, ray.dir));
        const Vec3 off = ray.origin + ray.dir * s - center;
        if (dot(off, off) <= (kScaleAllRadius * size) * (kScaleAllRadius * size)) {
            best = Handle::ScaleAll;
            best_t = s;
        }
    } else if (mode == GizmoMode::Rotate) {
        // Rings: closest approach to the circle, sampled (robust when a ring is seen edge-on).
        constexpr int kSamples = 72;
        for (Handle h : {Handle::RotateX, Handle::RotateY, Handle::RotateZ}) {
            const auto [a, b] = plane_basis(handle_axis(h));
            Vec3 prev = center + a * (kRingRadius * size);
            for (int i = 1; i <= kSamples; ++i) {
                const float ang = 2.0f * math::kPi * static_cast<float>(i) / kSamples;
                const Vec3 p = center + (a * std::cos(ang) + b * std::sin(ang)) * (kRingRadius * size);
                const auto [d2, t] = ray_segment(ray, prev, p);
                consider(h, d2, t);
                prev = p;
            }
        }
    }
    return best;
}

std::optional<GizmoDrag> GizmoDrag::begin(Handle handle, const Ray& ray, Vec3 center, Vec3 view_dir, float size,
                                          math::Quat orientation) {
    if (handle == Handle::None) return std::nullopt;
    GizmoDrag d;
    d.handle_ = handle;
    d.center_ = center;
    d.size_ = std::max(size, 1e-4f);
    const Vec3 axis = is_scale(handle) ? math::rotate(orientation, handle_axis(handle)) : handle_axis(handle);
    d.axis_ = axis;
    if (handle == Handle::ScaleAll) {
        d.plane_normal_ = -normalize(view_dir);
        const Vec3 right = normalize(cross(view_dir, {0, 1, 0}));
        d.diagonal_ = normalize(right + cross(right, view_dir));
    } else if (is_rotate(handle) || handle == Handle::MoveXZ) {
        d.plane_normal_ = axis;
    } else {
        // A plane that contains the axis and faces the camera as much as possible.
        const Vec3 n = cross(axis, cross(view_dir, axis));
        if (math::length(n) < 1e-4f) return std::nullopt;  // looking straight down the axis
        d.plane_normal_ = normalize(n);
    }
    const auto start = d.hit(ray);
    if (!start) return std::nullopt;
    d.start_ = *start;
    d.last_raw_ = d.raw_angle(*start);
    return d;
}

std::optional<Vec3> GizmoDrag::hit(const Ray& ray) const {
    const auto t = ray_plane(ray, center_, plane_normal_);
    if (!t) return std::nullopt;
    return ray.origin + ray.dir * *t;
}

float GizmoDrag::raw_angle(Vec3 p) const {
    const auto [a, b] = plane_basis(plane_normal_);
    const Vec3 v = p - center_;
    return std::atan2(dot(v, b), dot(v, a));
}

Vec3 GizmoDrag::translation(const Ray& ray) const {
    const auto p = hit(ray);
    if (!p) return {};
    const Vec3 delta = *p - start_;
    if (handle_ == Handle::MoveXZ) return {delta.x, 0.0f, delta.z};
    return axis_ * dot(delta, axis_);
}

Vec3 GizmoDrag::scale(const Ray& ray) const {
    const auto p = hit(ray);
    if (!p) return {1, 1, 1};
    if (handle_ == Handle::ScaleAll) {
        const float f = std::max(0.01f, 1.0f + dot(*p - start_, diagonal_) / size_);
        return {f, f, f};
    }
    const float from = dot(start_ - center_, axis_);
    if (std::fabs(from) < 1e-4f) return {1, 1, 1};
    const float f = std::max(0.01f, dot(*p - center_, axis_) / from);
    Vec3 r{1, 1, 1};
    (axis_index(handle_) == 0 ? r.x : axis_index(handle_) == 1 ? r.y : r.z) = f;
    return r;
}

float GizmoDrag::angle(const Ray& ray) {
    const auto p = hit(ray);
    if (!p) return total_;
    const float raw = raw_angle(*p);
    float step = raw - last_raw_;
    if (step > math::kPi) step -= 2.0f * math::kPi;
    if (step < -math::kPi) step += 2.0f * math::kPi;
    last_raw_ = raw;
    total_ += step;
    return total_;
}

} // namespace noor::editor
