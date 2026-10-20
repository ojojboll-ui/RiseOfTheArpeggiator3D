#pragma once

#include <vector>

#include "World.h"

/**
    A player that is not you.

    It runs the menu's attract mode and the --demo test runs. In the 3D game's
    larger level (2026-10-19) it tours the song: to the verse's tower, then the
    chorus's, then the bridge's, finding its way round walls, circling each
    tower while it shoots it down, and to the gate when that opens. On the way
    it flees what comes close, shoots what is near, and bombs when cornered.
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
            const Level& lvl = world.ground();

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

            // Where to go: the gate once it is open, else the nearest standing
            // tower of the first zone, in song order — verse, chorus, bridge —
            // that still has any.
            const World::Tower* tower = nullptr;
            for (int z = 0; z < 3 && ! tower; ++z)
            {
                float best = 1.0e9f;
                for (const auto& t : world.towerList())
                {
                    if (t.zone != z || ! t.alive || ! t.active) continue;
                    const float d = (t.pos - me).length();
                    if (d < best) { best = d; tower = &t; }
                }
            }
            const Vec2 goal = world.gateState().open ? world.gateState().pos : (tower ? tower->pos : lvl.start());

            flowTimer -= dt;
            if (flowTimer <= 0.0f) { flowTimer = 0.3f; lvl.updateFlow (flow, goal); }

            Vec2 path;
            const float goalD = (goal - me).length();
            if (lvl.lineOfSight (me, goal)) path = (goal - me).normalised();
            else path = lvl.flowDirection (flow, me);

            // Near a tower: circle it at shooting distance instead of ramming it.
            if (tower && ! world.gateState().open && goalD < 480.0f && lvl.lineOfSight (me, goal))
            {
                const Vec2 out = (me - tower->pos).normalised();
                path = out.perp() * 0.9f + out * (goalD < 340.0f ? 0.7f : -0.2f);
            }

            // The stick pushed all the way in the direction it means to go, as a
            // player does, so the chord wheel follows it (2026-10-15).
            c.move = flee + path * 0.9f;
            if (c.move.length() > 0.05f) c.move = c.move.normalised();

            if (! reckless)
            {
                // Shoot what is close; else the tower, when it can be seen; else
                // whatever is in sight. Near a tower only what is really close
                // takes the aim off it.
                const bool atTower = tower && goalD < 650.0f && lvl.lineOfSight (me, tower->pos);
                if (nearest && nearestD < (atTower ? 260.0f : 520.0f) && lvl.lineOfSight (me, nearest->pos))
                    aimAt (c, me, nearest->pos + nearest->vel * (nearestD / kBulletSpeed));
                else if (tower && goalD < 850.0f && lvl.lineOfSight (me, tower->pos))
                    aimAt (c, me, tower->pos);
                else if (nearest && nearestD < 900.0f && lvl.lineOfSight (me, nearest->pos))
                    aimAt (c, me, nearest->pos + nearest->vel * (nearestD / kBulletSpeed));
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
        static void aimAt (Controls& c, Vec2 me, Vec2 at)
        {
            c.aim = (at - me).normalised();
            c.fire = true;
        }

        std::vector<World::Threat> seen;
        Level::Flow flow;
        float time = 0.0f, bombCooldown = 0.0f, flowTimer = 0.0f;
    };
}
