#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace np
{
    constexpr float kPi  = 3.14159265358979f;
    constexpr float kTau = 6.28318530717959f;

    struct Vec2
    {
        float x = 0.0f, y = 0.0f;

        Vec2() = default;
        constexpr Vec2 (float xIn, float yIn) : x (xIn), y (yIn) {}

        Vec2 operator+ (Vec2 o) const { return { x + o.x, y + o.y }; }
        Vec2 operator- (Vec2 o) const { return { x - o.x, y - o.y }; }
        Vec2 operator* (float s) const { return { x * s, y * s }; }
        Vec2 operator/ (float s) const { return { x / s, y / s }; }
        Vec2 operator- () const { return { -x, -y }; }
        Vec2& operator+= (Vec2 o) { x += o.x; y += o.y; return *this; }
        Vec2& operator-= (Vec2 o) { x -= o.x; y -= o.y; return *this; }
        Vec2& operator*= (float s) { x *= s; y *= s; return *this; }

        float length() const { return std::sqrt (x * x + y * y); }
        float lengthSq() const { return x * x + y * y; }

        Vec2 normalised() const
        {
            const float l = length();
            return l > 1.0e-6f ? Vec2 { x / l, y / l } : Vec2 { 0.0f, 0.0f };
        }

        Vec2 perp() const { return { -y, x }; }
        float angle() const { return std::atan2 (y, x); }
    };

    inline float dot (Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    inline Vec2 fromAngle (float a, float len = 1.0f) { return { std::cos (a) * len, std::sin (a) * len }; }
    inline Vec2 rotate (Vec2 v, float a)
    {
        const float c = std::cos (a), s = std::sin (a);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }
    inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
    inline Vec2 lerp (Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

    /** Signed smallest difference between two angles, in radians. */
    inline float angleDiff (float a, float b)
    {
        float d = std::fmod (a - b + kPi, kTau);
        if (d < 0.0f) d += kTau;
        return d - kPi;
    }

    struct Color
    {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

        Color() = default;
        constexpr Color (float rIn, float gIn, float bIn, float aIn = 1.0f) : r (rIn), g (gIn), b (bIn), a (aIn) {}

        Color operator* (float s) const { return { r, g, b, a * s }; }
        Color withAlpha (float alpha) const { return { r, g, b, alpha }; }
    };

    inline Color mix (Color a, Color b, float t)
    {
        return { lerp (a.r, b.r, t), lerp (a.g, b.g, t), lerp (a.b, b.b, t), lerp (a.a, b.a, t) };
    }

    inline Color hsv (float h, float s, float v, float a = 1.0f)
    {
        h = std::fmod (h, 1.0f); if (h < 0.0f) h += 1.0f;
        const float i = std::floor (h * 6.0f);
        const float f = h * 6.0f - i;
        const float p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s);
        switch ((int) i % 6)
        {
            case 0:  return { v, t, p, a };
            case 1:  return { q, v, p, a };
            case 2:  return { p, v, t, a };
            case 3:  return { p, q, v, a };
            case 4:  return { t, p, v, a };
            default: return { v, p, q, a };
        }
    }

    /** Xorshift. The game's randomness; the audio thread has its own. */
    struct Rng
    {
        std::uint32_t s = 0x2f6b9a1du;

        std::uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float unit() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
        float range (float lo, float hi) { return lo + (hi - lo) * unit(); }
        int   below (int n) { return n > 0 ? (int) (next() % (std::uint32_t) n) : 0; }
        bool  chance (float p) { return unit() < p; }
        Vec2  inDisc (float r) { const float a = range (0.0f, kTau); return fromAngle (a, r * std::sqrt (unit())); }
    };
}
