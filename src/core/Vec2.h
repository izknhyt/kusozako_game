#pragma once

#include <cmath>

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator+(const Vec2 &a, const Vec2 &b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(const Vec2 &a, const Vec2 &b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(const Vec2 &a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(const Vec2 &a, float s) { return {a.x / s, a.y / s}; }
inline Vec2 &operator+=(Vec2 &a, const Vec2 &b)
{
    a.x += b.x;
    a.y += b.y;
    return a;
}
inline Vec2 lerp(const Vec2 &a, const Vec2 &b, float t) { return a + (b - a) * t; }
inline float dot(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }
inline float lengthSq(const Vec2 &v) { return dot(v, v); }
inline float length(const Vec2 &v) { return std::sqrt(lengthSq(v)); }
inline Vec2 normalize(const Vec2 &v)
{
    const float len = length(v);
    return len > 0.0001f ? v / len : Vec2{0.0f, 0.0f};
}

