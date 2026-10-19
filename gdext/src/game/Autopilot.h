#pragma once

#include <vector>

#include "World.h"

/**
    A player that is not you.

    It runs the menu's attract mode and the --demo test runs: it flees what is
    close, drifts round the field (which walks the chord wheel — circling is the
    progression), shoots the nearest thing it can see and bombs when cornered.
    It plays badly enough to die now and then, which is what the tests need.
*/
namespace np
{
    class Autopilot
    {
    public:
        bool reckless = false;     // flies blind and unarmed, for testing deaths
        bool slowTest = false;     // holds slow motion 3 s in every 12, for testing it
        Controls think (const World& world, float dt)
        {
            Controls c;
            time += dt;

            world.threats (seen);
            const Vec2 me = world.shipPos();

            Vec2 flee;
            const World::Threat* nearest = nullptr;
            float nearestD = 1.0e9f;

            for (const auto& t : seen)
            {
                const Vec2 d = me - t.pos;
                const float dist = std::max (1.0f, d.length());
                if (dist < 340.0f && ! reckless) flee += d / dist * ((340.0f - dist) / 340.0f) * (t.live ? 2.2f : 0.8f);
                if (t.live && dist < nearestD) { nearestD = dist; nearest = &t; }
            }

            // Drift round the centre, so the stick visits every chord.
            const Vec2 centre { (kFieldL + kFieldR) * 0.5f, (kFieldT + kFieldB) * 0.5f };
            const Vec2 fromCentre = me - centre;
            Vec2 orbit = fromCentre.perp().normalised() * 0.55f;
            orbit += (centre + fromAngle (time * 0.21f, 330.0f) - me) * 0.0025f;

            // The stick pushed all the way in the direction it means to go, as a
            // player does. Half-pushed, it rarely crossed the wheel's threshold
            // and the attract mode hardly changed chord (2026-10-15).
            c.move = flee + orbit;
            if (c.move.length() > 0.05f) c.move = c.move.normalised();

            if (nearest && ! reckless)
            {
                const Vec2 lead = nearest->pos + nearest->vel * (nearestD / kBulletSpeed);
                c.aim = (lead - me).normalised();
                c.fire = true;
            }

            if (nearest && ! reckless && nearestD < 60.0f && world.bombsLeft() > 0 && bombCooldown <= 0.0f)
            {
                c.bomb = true;
                bombCooldown = 3.0f;
            }
            bombCooldown -= dt;

            if (slowTest) c.slow = std::fmod (time, 12.0f) > 9.0f;

            return c;
        }

    private:
        std::vector<World::Threat> seen;
        float time = 0.0f, bombCooldown = 0.0f;
    };
}
