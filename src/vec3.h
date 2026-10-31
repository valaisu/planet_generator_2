#pragma once

#include <cmath>

// Phase 1 / 3.1: Vector mathematics. Header-only; these are small and hot.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator-() const { return Vec3(-x, -y, -z); }
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(Vec3 v, float s) { return Vec3(v.x * s, v.y * s, v.z * s); }
inline Vec3 operator*(float s, Vec3 v) { return v * s; }
inline Vec3 operator/(Vec3 v, float s) { return v * (1.0f / s); }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(Vec3 v) { return v / length(v); }

// Rotate v about `axis` by `angle` radians (Rodrigues' rotation formula).
inline Vec3 rotate_about(Vec3 v, Vec3 axis, float angle) {
    axis = normalize(axis);
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}
