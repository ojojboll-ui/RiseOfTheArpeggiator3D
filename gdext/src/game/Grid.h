#pragma once

#include <vector>

#include "Math.h"

/**
    The floor: a sheet of point masses that move up and down.

    In the 2D game the floor was a mesh of springs pushed sideways, the
    Geometry Wars grid. Here the same idea stands up: each point has a height
    and is tied to its four neighbours and, weakly, to rest, so an explosion
    presses a dent into the floor that runs outward as a ring of waves and
    settles back. Black holes press a steady funnel. The kick presses the
    floor under the ship on every beat, so the ground pumps with the track.

    Heights are in the game's units (the 2D field's pixels), downward
    positive in push(), and the edge points never move.
*/
namespace np
{
    class Grid
    {
    public:
        void build (float left, float top, float right, float bottom, float spacing)
        {
            x0 = left; y0 = top;
            cols = (int) ((right - left) / spacing) + 1;
            rows = (int) ((bottom - top) / spacing) + 1;
            dx = (right - left) / (float) (cols - 1);
            dy = (bottom - top) / (float) (rows - 1);
            h.assign ((size_t) (cols * rows), 0.0f);
            v.assign ((size_t) (cols * rows), 0.0f);
        }

        /** Presses the floor down round a point, falling off with distance. */
        void push (Vec2 centre, float force, float radius)
        {
            forEachNear (centre, radius, [&] (size_t i, float dist) { v[i] -= force * (1.0f - dist / radius) * kPushScale; });
        }

        /** A black hole's funnel: a steady press, deepest at the centre. */
        void pull (Vec2 centre, float force, float radius)
        {
            forEachNear (centre, radius, [&] (size_t i, float dist)
            {
                const float k = 1.0f - dist / radius;
                v[i] -= force * k * k * kPushScale;
            });
        }

        void update (float dt)
        {
            // Two half steps keep the stiff springs stable at 60 Hz.
            for (int pass = 0; pass < 2; ++pass)
            {
                const float step = dt * 0.5f;
                const float damp = std::pow (0.04f, step);

                for (int r = 1; r < rows - 1; ++r)
                    for (int c = 1; c < cols - 1; ++c)
                    {
                        const size_t i = index (c, r);
                        const float around = h[i - 1] + h[i + 1] + h[i - (size_t) cols] + h[i + (size_t) cols] - 4.0f * h[i];
                        v[i] += (around * kStiff - h[i] * kAnchor) * step;
                    }

                for (size_t i = 0; i < h.size(); ++i)
                {
                    v[i] *= damp;
                    h[i] += v[i] * step;
                }

                // The rim is held.
                for (int c = 0; c < cols; ++c) { h[index (c, 0)] = h[index (c, rows - 1)] = 0.0f; }
                for (int r = 0; r < rows; ++r) { h[index (0, r)] = h[index (cols - 1, r)] = 0.0f; }
            }
        }

        /** The floor's height under a point, bilinear; positive is up. */
        float heightAt (Vec2 p) const
        {
            const float fx = std::clamp ((p.x - x0) / dx, 0.0f, (float) (cols - 1) - 0.001f);
            const float fy = std::clamp ((p.y - y0) / dy, 0.0f, (float) (rows - 1) - 0.001f);
            const int c = (int) fx, r = (int) fy;
            const float tx = fx - (float) c, ty = fy - (float) r;
            const float a = lerp (h[index (c, r)],     h[index (c + 1, r)],     tx);
            const float b = lerp (h[index (c, r + 1)], h[index (c + 1, r + 1)], tx);
            return lerp (a, b, ty);
        }

        const std::vector<float>& heights() const { return h; }
        int columns() const { return cols; }
        int rowCount() const { return rows; }
        float left() const { return x0; }
        float top() const { return y0; }
        float spacingX() const { return dx; }
        float spacingY() const { return dy; }

    private:
        // Wave speed is spacing x sqrt (kStiff): about 470 units a second.
        static constexpr float kAnchor = 9.0f;
        static constexpr float kStiff  = 140.0f;
        static constexpr float kPushScale = 0.35f;   // the 2D pushes were sideways and could be bigger

        size_t index (int c, int r) const { return (size_t) (r * cols + c); }

        template <typename F>
        void forEachNear (Vec2 centre, float radius, F&& f)
        {
            const int c0 = std::max (1, (int) ((centre.x - radius - x0) / dx));
            const int c1 = std::min (cols - 2, (int) ((centre.x + radius - x0) / dx) + 1);
            const int r0 = std::max (1, (int) ((centre.y - radius - y0) / dy));
            const int r1 = std::min (rows - 2, (int) ((centre.y + radius - y0) / dy) + 1);

            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                {
                    const Vec2 d = Vec2 { x0 + (float) c * dx, y0 + (float) r * dy } - centre;
                    const float dist = d.length();
                    if (dist < radius) f (index (c, r), dist);
                }
        }

        std::vector<float> h, v;
        int cols = 0, rows = 0;
        float x0 = 0, y0 = 0, dx = 1, dy = 1;
    };
}
