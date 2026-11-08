#pragma once
// Минимальная 2D-векторная алгебра. Используется double: точность важна для
// сравнения времени удара (TOI) с аналитикой, а производительность при ~100
// телах не ограничивает. Тип можно поменять в одном месте.
#include <cmath>

namespace phys {

using real = double;

struct Vec2 {
    real x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(real x_, real y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(real s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(real s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(real s) { x *= s; y *= s; return *this; }
};

constexpr Vec2 operator*(real s, Vec2 v) { return {v.x * s, v.y * s}; }
constexpr real dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline real length(Vec2 v) { return std::sqrt(dot(v, v)); }
// Линейная интерполяция: используется для отрисовки между шагами физики.
constexpr Vec2 lerp(Vec2 a, Vec2 b, real t) { return a + (b - a) * t; }

} // namespace phys
