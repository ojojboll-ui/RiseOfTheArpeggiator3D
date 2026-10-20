#include "World.h"

#include <cstdio>

namespace np
{
    using audio::Command;
    using audio::Event;

    //==========================================================================
    // Looks

    Color levelTint (int level)
    {
        static const Color tints[3] { { 0.22f, 0.36f, 1.0f }, { 0.50f, 1.0f, 0.18f }, { 0.85f, 0.25f, 1.0f } };
        return tints[std::clamp (level, 0, 2)];
    }

    Color levelAccent (int level)
    {
        static const Color accents[3] { { 0.35f, 0.85f, 1.0f }, { 1.0f, 0.95f, 0.25f }, { 1.0f, 0.45f, 0.85f } };
        return accents[std::clamp (level, 0, 2)];
    }

    /** Verse, chorus, bridge: the level's own colour for the verse, and two
        that stand well apart from it for the others. */
    Color zoneColor (int level, int zone)
    {
        static const Color c[3][3] {
            { { 0.22f, 0.36f, 1.0f }, { 1.0f, 0.28f, 0.80f }, { 1.0f, 0.68f, 0.15f } },     // PULSE: blue, magenta, amber
            { { 0.50f, 1.0f, 0.18f }, { 0.20f, 0.85f, 1.0f }, { 1.0f, 0.30f, 0.55f } },     // ACID: lime, cyan, pink
            { { 0.85f, 0.25f, 1.0f }, { 1.0f, 0.80f, 0.25f }, { 0.20f, 1.0f, 0.75f } } };   // STARFALL: violet, gold, teal
        return c[std::clamp (level, 0, 2)][std::clamp (zone, 0, 2)];
    }

    /** One colour per wheel slot, so the harmony can be seen: the ship's
        exhaust, the bullets and the bursts all wear the chord they belong to. */
    Color World::chordColor (int slot)
    {
        static const Color c[4] { { 0.25f, 0.92f, 1.0f },     // i    up
                                  { 1.0f, 0.30f, 0.85f },     // VI   right
                                  { 1.0f, 0.72f, 0.18f },     // III  down
                                  { 0.42f, 1.0f, 0.40f } };   // VII  left
        return c[slot & 3];
    }

    Color World::enemyColor (EnemyKind k)
    {
        switch (k)
        {
            case EnemyKind::wanderer: return { 0.72f, 0.38f, 1.0f };
            case EnemyKind::seeker:   return { 0.25f, 0.82f, 1.0f };
            case EnemyKind::dodger:   return { 1.0f, 0.82f, 0.20f };   // gold since 2026-10-20: green is the power-up's
            case EnemyKind::splitter: return { 1.0f, 0.30f, 0.80f };
            case EnemyKind::shard:    return { 1.0f, 0.55f, 0.85f };
            case EnemyKind::well:     return { 1.0f, 0.36f, 0.14f };
        }
        return {};
    }

    namespace
    {
        constexpr float kSparkGravity = 1800.0f;
        constexpr float kSlopePull = 520.0f;      // how hard hills roll enemies down, units/s² per unit of slope
        constexpr float kTowerHp = 160.0f;        // hits: about fifteen seconds of the arpeggio at 1/8; the second wave's
        constexpr float kFirstWaveHp = 80.0f;     // half as many for the first three (2026-10-20)
        constexpr float kRiseSeconds = 1.5f;
        constexpr float kBombRadius = 1700.0f;    // about a screen: the level is bigger than one now

        // How big a chord each kind lets out: 0 bright and short, 2 dark and long.
        int chordSize (EnemyKind k)
        {
            switch (k)
            {
                case EnemyKind::wanderer: case EnemyKind::shard: return 0;
                case EnemyKind::well:                            return 2;
                default:                                         return 1;
            }
        }
    }

    //==========================================================================

    World::World (audio::Conductor& bandIn, std::uint32_t seed) : band (bandIn)
    {
        rng.s = seed | 1u;
        lvl.build (0);
        grid.build (0.0f, 0.0f, lvl.width(), lvl.height(), 40.0f);
        grid.holdWhere ([this] (Vec2 p) { return lvl.solid (p); });
        ship.pos = lvl.start();
        placeTowers();   // the menu shows the first level as it stands at the start
    }

    void World::start (int levelIn, int difficultyIn, const Carry* carryIn)
    {
        diff = std::clamp (difficultyIn, 0, 2);
        level = std::clamp (levelIn, 0, 2);
        state = Phase::playing;

        lvl.build (level);
        grid.build (0.0f, 0.0f, lvl.width(), lvl.height(), 40.0f);
        grid.holdWhere ([this] (Vec2 p) { return lvl.solid (p); });
        chase = {};
        chaseTimer = 0.0f;

        ship = {};
        ship.pos = lvl.start();
        ship.invulnerable = 1.5f;

        placeTowers();
        shipZone = lastWaveZone = 0;

        enemies.clear(); incoming.clear(); bullets.clear(); particles.clear();
        pickups.clear(); popups.clear(); rings.clear();
        enemies.reserve (256); bullets.reserve (512); particles.reserve (8192);

        points = 0; lives = difficulty (diff).lives; bombs = difficulty (diff).bombs;
        if (carryIn != nullptr) { points = carryIn->score; lives = carryIn->lives; bombs = carryIn->bombs; }
        multiplier = 1; killsSinceDeath = 0;
        nextLifeAt = (points / kLifeEvery + 1) * kLifeEvery;
        nextBombAt = (points / kBombEvery + 1) * kBombEvery;
        wheelSlot = audio::song (level).homeSlot;
        deadTime = time = clearedTime = 0.0f;
        lastKicks = band.pubKicks.load();
        lastHalfBars = 0;
        slowMeter = 1.0f; timeScale = 1.0f; slowActive = false; slowProgress = 0.0f;
        kickPulse = chordFlash = flashAmount = shakeAmount = 0.0f;
        say (audio::song (level).name, 2.5f, levelAccent (level));

        band.requestedChord = wheelSlot;   // the song starts on its tonic
        band.requestedSection = 0;         // in the verse
        band.endless = true;               // the level ends at its gate, not with the song
        band.firing = false;
        // A fresh arpeggio direction at every start, for songs that draw one.
        band.post ({ Command::start, level, (int) (rng.next() % 4u) });
    }

    void World::placeTowers()
    {
        // One tower per zone and wave: the first three stand from the start,
        // the second three wait under the floor at other spots.
        for (int wave = 0; wave < 2; ++wave)
            for (int z = 0; z < 3; ++z)
            {
                auto& t = towers[(size_t) (wave * 3 + z)];
                t = {};
                t.pos = lvl.towerSpot (z, wave * 3);
                t.zone = z;
                t.wave = wave;
                t.active = wave == 0;
                t.rise = t.active ? 1.0f : 0.0f;
                t.hp = t.maxHp = wave == 0 ? kFirstWaveHp : kTowerHp;
            }
        for (int d = 0; d < Level::kDoors; ++d)
        {
            doors[(size_t) d] = {};
            doors[(size_t) d].zone = d % 3;
            doors[(size_t) d].wave = d / 3;
        }
        gate = {};
        gate.pos = lvl.gate();
    }

    void World::say (const std::string& text, float life, Color color)
    {
        banner = text;
        bannerLife = life;
        bannerColor = color;
    }

    int World::towersLeft() const
    {
        int n = 0;
        for (const auto& t : towers) if (t.alive) ++n;
        return n;
    }

    int World::doorsLeft() const
    {
        int n = 0;
        for (const auto& d : doors) if (! d.open) ++n;
        return n;
    }

    //==========================================================================
    // Events from the band

    void World::onEvent (const Event& e)
    {
        switch (e.type)
        {
            case Event::shot:
                if (state == Phase::playing && ship.alive)
                    spawnShot (e.a, e.b, e.c, e.d != 0);
                break;

            case Event::bar:
                if (state == Phase::playing)
                {
                    spawnWave (e.a, false);
                    // Every other bar one of the towers in the ship's room sends out enemies.
                    if (e.a % 2 == 0 && ship.alive)
                    {
                        Tower* pick[kTowers]; int n = 0;
                        for (auto& t : towers)
                            if (t.shootable() && t.zone == shipZone) pick[n++] = &t;
                        if (n > 0) towerBurst (*pick[rng.below (n)]);
                    }
                }
                break;

            case Event::chord:
                chordFlash = 1.0f;
                break;

            case Event::section:
                say (audio::sectionName (e.a), 1.6f, zoneColor (level, e.a));
                chordFlash = 1.0f;
                break;

            case Event::layerUp:
                say (std::string ("+ ") + audio::layerName (audio::song (level), e.a), 2.2f, levelAccent (level));
                flashAmount = std::max (flashAmount, 0.35f);
                grid.push (ship.pos, 900.0f, 700.0f);
                break;

            case Event::layerDown:
                say (std::string ("- ") + audio::layerName (audio::song (level), e.a + 1), 2.2f, { 1.0f, 0.25f, 0.2f });
                break;

            case Event::dropEnd:
                if (state == Phase::dead) respawn();
                break;

            case Event::songEnd:
                break;
        }
    }

    //==========================================================================

    void World::update (float dt, const Controls& in)
    {
        time += dt;

        // The kick, as the band counts them.
        const int kicks = band.pubKicks.load();
        if (kicks != lastKicks)
        {
            lastKicks = kicks;
            kickPulse = 1.0f;
            if (ship.alive) grid.push (ship.pos, 160.0f, 260.0f);
            for (const auto& e : enemies)
                if (e.alive && e.kind == EnemyKind::well && e.spawn <= 0.0f) grid.pull (e.pos, 220.0f, 260.0f);
        }

        // From the third layer on, a second, smaller wave lands on every half bar.
        const int halfBars = (int) (band.beatPosition() / 2.0);
        if (halfBars != lastHalfBars)
        {
            lastHalfBars = halfBars;
            if (halfBars % 2 == 1 && band.pubLayer.load() >= difficulty (diff).halfBarLayer && state == Phase::playing)
                spawnWave (halfBars / 2, true);
        }

        kickPulse *= std::exp (-dt * 7.0f);
        chordFlash *= std::exp (-dt * 5.0f);
        flashAmount *= std::exp (-dt * 4.0f);
        bannerLife -= dt;

        shakeAmount *= std::exp (-dt * 9.0f);
        shakeVec = shakeAmount > 0.3f ? rng.inDisc (shakeAmount) : Vec2 {};

        if (state == Phase::playing)
        {
            const float slowA = std::clamp ((1.0f - timeScale) / (1.0f - kSlowSpeed), 0.0f, 1.0f);
            updateShip (dt * (1.0f - (1.0f - kSlowShipSpeed) * slowA), in);

            if (in.bomb) detonateBomb();

            // Slow motion: needs a little in the meter to start, runs it dry.
            const bool want = in.slow && ship.alive && (slowActive ? slowMeter > 0.0f : slowMeter > 0.15f);
            setSlow (want);
            if (slowActive)
            {
                slowMeter = std::max (0.0f, slowMeter - kSlowDrain * dt);
                slowProgress = std::clamp ((slowStartMeter - slowMeter) / slowStartMeter, 0.0f, 1.0f);
            }
        }
        else
        {
            band.firing = false;
            setSlow (false);
        }

        // The world's clock eases between full speed and slow, so it never jerks.
        timeScale += ((slowActive ? kSlowSpeed : 1.0f) - timeScale) * std::min (1.0f, dt * 12.0f);
        const float realDt = dt;
        dt *= timeScale;

        // The band's slow filter: a noticeable but gentle dip as slow motion
        // starts, then down with the zoom on the same logarithmic course.
        {
            const float depth = slowAmount() * (kSlowDip + (1.0f - kSlowDip) * slowProgress);
            if (std::abs (depth - sentSlowDepth) > 0.002f || (depth == 0.0f && sentSlowDepth != 0.0f))
            {
                sentSlowDepth = depth;
                band.post ({ Command::slow, depth > 0.0f ? 1 : 0, 0, depth });
            }
        }

        if (state == Phase::dead)
        {
            deadTime += dt;
            if (deadTime > 6.0f) respawn();   // in case the music cannot bring it back
        }

        if (state == Phase::cleared) clearedTime += dt;

        // Way-finding towards the ship, kept fresh as it moves.
        chaseTimer -= realDt;
        if (chaseTimer <= 0.0f)
        {
            chaseTimer = 0.12f;
            lvl.updateFlow (chase, ship.alive ? ship.pos : lvl.start());
        }

        updateZone();
        updateEnemies (dt);
        updateTowers (dt);
        updateGate (realDt);
        updateBullets (realDt);   // the shots are the player's, so they keep full speed
        updatePickups (dt);
        collide();
        updateParticles (dt);
        grid.update (dt);

        for (auto& p : popups) { p.life -= dt; p.pos.y -= 40.0f * dt; }
        popups.erase (std::remove_if (popups.begin(), popups.end(), [] (const Popup& p) { return p.life <= 0.0f; }), popups.end());

        for (auto& r : rings) { r.radius += r.speed * dt; r.life -= dt; }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.life <= 0.0f; }), rings.end());

        enemies.erase (std::remove_if (enemies.begin(), enemies.end(), [] (const Enemy& e) { return ! e.alive; }), enemies.end());
        enemies.insert (enemies.end(), incoming.begin(), incoming.end());
        incoming.clear();
        bullets.erase (std::remove_if (bullets.begin(), bullets.end(), [] (const Bullet& b) { return ! b.alive; }), bullets.end());
        pickups.erase (std::remove_if (pickups.begin(), pickups.end(), [] (const Pickup& p) { return ! p.alive; }), pickups.end());
    }

    void World::setSlow (bool on)
    {
        if (on == slowActive) return;
        slowActive = on;
        if (on) { slowStartMeter = std::max (0.01f, slowMeter); slowProgress = 0.0f; }
    }

    void World::updateAmbient (float dt)
    {
        time += dt;

        const int kicks = band.pubKicks.load();
        if (kicks != lastKicks) { lastKicks = kicks; kickPulse = 1.0f; }
        kickPulse *= std::exp (-dt * 7.0f);

        // A slow drift of disturbances round the start, so the menu floor is never still.
        if (rng.chance (dt * 1.4f))
        {
            const Vec2 p = lvl.start() + Vec2 { rng.range (-800.0f, 800.0f), rng.range (-500.0f, 400.0f) };
            if (! lvl.solid (p))
            {
                grid.push (p, rng.range (300.0f, 700.0f), 220.0f);
                explode (p, hsv (rng.unit(), 0.7f, 1.0f), hsv (rng.unit(), 0.5f, 1.0f), 40, 500.0f, 1.2f);
            }
        }

        updateParticles (dt);
        grid.update (dt);
    }

    //==========================================================================
    // Ship

    void World::updateChordRequest (Vec2 move)
    {
        // The wheel reads the stick only when it is clearly pushed, and keeps
        // the current chord until the stick is well inside a neighbour's
        // quarter — 12 degrees past the boundary.
        if (move.length() < 0.5f) return;

        const float angle = std::atan2 (move.x, -move.y);     // 0 up, clockwise
        const float centre = (float) wheelSlot * kPi * 0.5f;
        const float hysteresis = (45.0f + 12.0f) * kPi / 180.0f;

        if (std::abs (angleDiff (angle, centre)) > hysteresis)
        {
            float a = std::fmod (angle + kTau + kPi * 0.25f, kTau);
            wheelSlot = (int) (a / (kPi * 0.5f)) & 3;
        }

        band.requestedChord = wheelSlot;
    }

    void World::updateZone()
    {
        // The section follows the room the ship is in; in a passage it keeps
        // the one it came from, so the music turns as the ship arrives.
        const int z = lvl.zoneAt (ship.pos);
        if (z >= 0 && z != shipZone && ship.alive)
        {
            shipZone = z;
            band.requestedSection = z;
        }
    }

    Vec2 World::keepOut (Vec2 p, float radius, Vec2* normal) const
    {
        Vec2 n;
        Vec2 q = lvl.resolve (p, radius, &n);
        for (const auto& t : towers)
        {
            if (! t.solidNow()) continue;
            const Vec2 d = q - t.pos;
            const float dist = d.length(), minD = t.radius + radius;
            if (dist < minD && dist > 1.0e-3f) { q = t.pos + d / dist * minD; n = d / dist; }
        }
        if (normal != nullptr) *normal = n;
        return q;
    }

    void World::updateShip (float dt, const Controls& in)
    {
        if (! ship.alive) { band.firing = false; return; }

        // Speed follows deflection with a slight curve, so small corrections
        // are fine; this is a position control, not something heard.
        Vec2 move = in.move;
        const float m = std::min (1.0f, move.length());
        if (m > 0.0f) move = move.normalised() * std::pow (m, 1.3f);

        // Hills: a little slower up, a little faster down.
        const float climb = m > 0.0f ? dot (lvl.slope (ship.pos), move.normalised()) : 0.0f;
        const float hill = std::clamp (1.0f - climb * 1.2f, 0.75f, 1.25f);

        const Vec2 target = move * (kShipSpeed * hill);
        ship.vel += (target - ship.vel) * std::min (1.0f, dt * 14.0f);
        ship.pos += ship.vel * dt;

        // Wells pull the ship as well.
        for (const auto& e : enemies)
        {
            if (! e.alive || e.kind != EnemyKind::well || e.spawn > 0.0f) continue;
            const Vec2 d = e.pos - ship.pos;
            const float dist = d.length();
            if (dist < 420.0f && dist > 1.0f) ship.pos += d / dist * (120.0f * (1.0f - dist / 420.0f) * dt);
        }

        ship.pos = keepOut (ship.pos, 18.0f);

        if (ship.vel.length() > 40.0f)
        {
            const float want = ship.vel.angle();
            ship.angle += angleDiff (want, ship.angle) * std::min (1.0f, dt * 16.0f);

            // Exhaust in the colour of the chord being played.
            const int n = (int) (ship.vel.length() / kShipSpeed * 3.0f) + 1;
            const Color c = chordColor (band.pubChord.load());
            for (int i = 0; i < n; ++i)
            {
                Particle p;
                p.pos = ship.pos - fromAngle (ship.angle, 14.0f);
                p.vel = -ship.vel.normalised() * rng.range (150.0f, 380.0f) + rng.inDisc (90.0f);
                p.color = c.withAlpha (0.7f);
                p.life = p.maxLife = rng.range (0.25f, 0.55f);
                p.width = 2.2f;
                p.drag = 0.9f;
                p.z = 10.0f;
                p.vz = rng.range (-20.0f, 60.0f);
                particles.push_back (p);
            }
        }

        if (ship.invulnerable > 0.0f) ship.invulnerable -= dt;

        updateChordRequest (in.move);

        const bool firing = in.fire && in.aim.length() > 0.2f;
        band.firing = firing;
        if (firing) aimDir = in.aim.normalised();
        // The camera follows the ship, so the arpeggio pans with the aim.
        band.aimPan = 0.5f + 0.2f * aimDir.x;
    }

    void World::spawnShot (int note, int chord, int index, bool accent)
    {
        const int rate = band.pubArpRate.load();
        const float base = aimDir.angle();

        // The arpeggio fans the shots a little: each step of the cycle leaves
        // at its own angle, so a run up the chord is a sweep across the aim.
        const float fan = ((float) index / 100.0f - 0.5f) * 0.084f;
        const Color c = chordColor (chord);

        auto fire = [&] (float angle, Vec2 offset)
        {
            Bullet b;
            b.pos = ship.pos + fromAngle (base, 20.0f) + offset;
            b.vel = fromAngle (angle, kBulletSpeed);
            b.chord = chord;
            b.note = note;
            b.accent = accent;
            bullets.push_back (b);
        };

        if (rate == 0)
        {
            for (int k = -1; k <= 1; ++k) fire (base + fan + 0.10f * (float) k, {});
        }
        else
        {
            const Vec2 side = fromAngle (base + kPi * 0.5f, 8.0f);
            fire (base + fan, side);
            fire (base + fan, -side);
            if (rate == 2) fire (base + fan, {});
        }

        Particle flashP;
        flashP.pos = ship.pos + fromAngle (base, 26.0f);
        flashP.vel = fromAngle (base, 300.0f);
        flashP.color = c.withAlpha (accent ? 1.0f : 0.6f);
        flashP.life = flashP.maxLife = 0.08f;
        flashP.width = accent ? 9.0f : 6.0f;
        flashP.z = 16.0f;
        particles.push_back (flashP);
    }

    //==========================================================================
    // Enemies

    void World::spawnEnemy (EnemyKind kind, Vec2 pos, float delay)
    {
        if (enemies.size() + incoming.size() >= 220) return;
        if (lvl.solid (pos)) return;

        Enemy e;
        e.kind = kind;
        e.pos = keepOut (pos, 30.0f);
        e.spawn = 0.7f + delay;
        e.angle = rng.range (0.0f, kTau);
        e.wander = rng.range (0.0f, kTau);

        switch (kind)
        {
            case EnemyKind::wanderer: e.radius = 20.0f; e.value = 25;  e.spin = 3.0f; break;
            case EnemyKind::seeker:   e.radius = 18.0f; e.value = 50;  break;
            case EnemyKind::dodger:   e.radius = 19.0f; e.value = 100; e.spin = 1.2f; break;
            case EnemyKind::splitter: e.radius = 22.0f; e.value = 75;  e.spin = 1.5f; break;
            case EnemyKind::shard:    e.radius = 10.0f; e.value = 50;  e.spin = 6.0f; e.spawn = delay; break;
            case EnemyKind::well:     e.radius = 26.0f; e.value = 300; e.hp = 14.0f; break;
        }

        incoming.push_back (e);
    }

    Vec2 World::farPoint (float minDist)
    {
        for (int tries = 0; tries < 30; ++tries)
        {
            const Vec2 p = lvl.randomPoint (shipZone, rng, 50.0f);
            if ((p - ship.pos).length() >= minDist) return p;
        }
        const Rect& r = lvl.roomRect (shipZone);
        return r.at (ship.pos.x < r.centre().x ? 0.85f : 0.15f, 0.5f);
    }

    void World::spawnWave (int bar, bool half)
    {
        // The first bar is a breath: kick and bass, nothing to shoot.
        if (bar < 1 || ! ship.alive) return;

        const int layer = band.pubLayer.load();
        const float intensity = (0.5f * (float) level + 0.35f * (float) layer + (float) std::min (bar, 112) / 28.0f) * difficulty (diff).spawn;
        const int full = std::min ((int) (18.0f * difficulty (diff).spawn), std::max (2, (int) ((3.0f + intensity * 2.2f) * std::sqrt (difficulty (diff).spawn))));
        const int budget = half ? std::max (2, full / 2) : full;

        int wells = 0;
        for (const auto& e : enemies) if (e.alive && e.kind == EnemyKind::well) ++wells;

        // Waves come into the room the ship is in (or last was).
        const Rect room = lvl.roomRect (shipZone);

        // What this level's floor can throw. Each level adds a kind.
        struct Option { int kind; float weight; };
        std::vector<Option> options {
            { 0, 1.0f },                                  // wanderers along an edge
            { 1, 1.0f },                                  // seekers from the corners
            { 2, 0.7f },                                  // dodgers in a cluster
            { 3, level >= 1 ? 0.6f : 0.0f },              // splitters
            { 4, ! half && (level >= 2 || (level == 1 && layer >= 3)) && wells < 2 ? 0.25f : 0.0f },   // a well
            { 5, ! half && intensity > 2.5f && bar % 8 == 4 ? 2.0f : 0.0f }                             // a ring round the ship
        };

        float total = 0.0f;
        for (const auto& o : options) total += o.weight;
        float pick = rng.range (0.0f, total);
        int choice = 0;
        for (const auto& o : options) { if ((pick -= o.weight) <= 0.0f) { choice = o.kind; break; } }

        switch (choice)
        {
            case 0:
            {
                const int edge = rng.below (4);
                for (int i = 0; i < budget; ++i)
                {
                    const float t = ((float) i + 0.5f) / (float) budget;
                    const float in = 60.0f;
                    Vec2 p;
                    if (edge == 0) p = { lerp (room.x0, room.x1, t), room.y0 + in };
                    if (edge == 1) p = { room.x1 - in, lerp (room.y0, room.y1, t) };
                    if (edge == 2) p = { lerp (room.x0, room.x1, t), room.y1 - in };
                    if (edge == 3) p = { room.x0 + in, lerp (room.y0, room.y1, t) };
                    if ((p - ship.pos).length() > 250.0f) spawnEnemy (EnemyKind::wanderer, p, 0.04f * (float) i);
                }
                break;
            }

            case 1:
            {
                const Vec2 corners[4] { { room.x0 + 80, room.y0 + 80 }, { room.x1 - 80, room.y0 + 80 },
                                        { room.x1 - 80, room.y1 - 80 }, { room.x0 + 80, room.y1 - 80 } };
                const int per = std::max (1, budget / 4);
                for (const auto& c : corners)
                {
                    if ((c - ship.pos).length() < 300.0f) continue;
                    for (int i = 0; i < per; ++i) spawnEnemy (EnemyKind::seeker, c + rng.inDisc (40.0f), 0.08f * (float) i);
                }
                break;
            }

            case 2:
            {
                const Vec2 c = farPoint (500.0f);
                for (int i = 0; i < std::max (2, budget * 2 / 3); ++i)
                    spawnEnemy (EnemyKind::dodger, c + rng.inDisc (90.0f), 0.05f * (float) i);
                break;
            }

            case 3:
                for (int i = 0; i < std::max (1, budget / 3); ++i)
                    spawnEnemy (EnemyKind::splitter, farPoint (450.0f), 0.1f * (float) i);
                break;

            case 4:
                spawnEnemy (EnemyKind::well, farPoint (550.0f));
                for (int i = 0; i < budget / 2; ++i) spawnEnemy (EnemyKind::wanderer, farPoint (400.0f), 0.1f * (float) i);
                break;

            case 5:
            {
                const int n = std::min (12, budget);
                for (int i = 0; i < n; ++i)
                    spawnEnemy (EnemyKind::seeker, ship.pos + fromAngle (kTau * (float) i / (float) n, 470.0f), 0.3f);
                break;
            }
        }
    }

    void World::towerBurst (Tower& t)
    {
        // A handful from the tower's foot: seekers, and from the second level
        // on something heavier now and then.
        const int n = 2 + std::min (3, band.pubLayer.load() / 2) + (diff == 2 ? 1 : 0);
        const EnemyKind kind = (level >= 1 && rng.chance (0.3f)) ? EnemyKind::splitter
                             : (rng.chance (0.35f) ? EnemyKind::dodger : EnemyKind::seeker);
        for (int i = 0; i < n; ++i)
            spawnEnemy (kind, t.pos + fromAngle (kTau * (float) i / (float) n + rng.unit(), t.radius + 50.0f), 0.08f * (float) i);
        rings.push_back ({ t.pos, t.radius, 600.0f, 0.4f, zoneColor (level, t.zone) });
        grid.push (t.pos, 400.0f, 260.0f);
    }

    Vec2 World::chaseDirection (Vec2 from) const
    {
        const Vec2 target = ship.alive ? ship.pos : lvl.start();
        const Vec2 d = target - from;
        const float dist = d.length();
        if (dist < 1.0f) return {};

        // Straight at it when it can be seen; round the walls when it cannot.
        if (dist < 900.0f && lvl.lineOfSight (from, target)) return d / dist;
        const Vec2 f = lvl.flowDirection (chase, from);
        return f.length() > 0.0f ? f : d / dist;
    }

    void World::updateEnemies (float dt)
    {
        const int layer = band.pubLayer.load();
        const float speedUp = (1.0f + 0.08f * (float) level + 0.03f * (float) layer) * difficulty (diff).speed;

        for (auto& e : enemies)
        {
            if (! e.alive) continue;

            e.flash = std::max (0.0f, e.flash - dt * 6.0f);
            e.angle += e.spin * dt;

            if (e.spawn > 0.0f) { e.spawn -= dt; continue; }

            const Vec2 toward = chaseDirection (e.pos);

            switch (e.kind)
            {
                case EnemyKind::wanderer:
                {
                    e.wander += rng.range (-2.5f, 2.5f) * dt;
                    const Vec2 want = fromAngle (e.wander, 150.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 2.0f);
                    break;
                }

                case EnemyKind::seeker:
                {
                    const Vec2 want = toward * (230.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 2.5f);
                    e.angle = e.vel.angle();
                    break;
                }

                case EnemyKind::dodger:
                {
                    Vec2 want = toward * (210.0f * speedUp);

                    // Sidestep any bullet heading this way.
                    for (const auto& b : bullets)
                    {
                        const Vec2 d = e.pos - b.pos;
                        const float bd = d.length();
                        if (bd < 140.0f && dot (d, b.vel) > 0.0f)
                        {
                            const Vec2 side = b.vel.normalised().perp();
                            want += side * (dot (side, d) > 0.0f ? 520.0f : -520.0f);
                            break;
                        }
                    }
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 6.0f);
                    break;
                }

                case EnemyKind::splitter:
                {
                    const Vec2 want = toward * (150.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 1.6f);
                    break;
                }

                case EnemyKind::shard:
                {
                    // Circle in on the ship rather than straight at it.
                    const Vec2 want = (toward + toward.perp() * 0.8f).normalised() * (300.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 3.0f);
                    break;
                }

                case EnemyKind::well:
                {
                    e.vel *= std::exp (-dt * 1.5f);
                    e.radius = 26.0f + e.eaten * 1.6f + 4.0f * kickPulse;
                    grid.pull (e.pos, 30.0f * dt * 60.0f * 0.25f, 220.0f);

                    // Swallow anything that strays too close.
                    for (auto& o : enemies)
                    {
                        if (&o == &e || ! o.alive || o.kind == EnemyKind::well || o.spawn > 0.0f) continue;
                        const Vec2 d = e.pos - o.pos;
                        const float od = d.length();
                        if (od < 360.0f && od > 1.0f) o.vel += d / od * (900.0f * (1.0f - od / 360.0f) * dt);
                        if (od < e.radius + o.radius * 0.5f)
                        {
                            o.alive = false;
                            e.eaten += 1.0f;
                            e.hp += 1.0f;
                            explode (o.pos, enemyColor (o.kind), enemyColor (EnemyKind::well), 20, 300.0f, 0.6f);
                        }
                    }

                    // Overfed: it bursts and lets out what it ate.
                    if (e.eaten >= 12.0f)
                    {
                        e.alive = false;
                        explode (e.pos, enemyColor (EnemyKind::well), { 1, 1, 1 }, 260, 1100.0f, 1.8f);
                        grid.push (e.pos, 1600.0f, 500.0f);
                        band.post ({ Command::kill, band.pubChord.load(), 2, panOf (e.pos), -1, (int) EnemyKind::well });
                        for (int i = 0; i < 8; ++i) spawnEnemy (EnemyKind::shard, e.pos + fromAngle (kTau * i / 8.0f, 30.0f), 0.0f);
                        shakeAmount = std::max (shakeAmount, 12.0f);
                    }
                    break;
                }
            }

            // Hills roll everything a little downhill.
            e.vel -= lvl.slope (e.pos) * (kSlopePull * dt);

            e.pos += e.vel * dt;

            // Walls and towers: pushed out, bounced off. The wander heading is
            // only turned while it still points into the wall — turning it every
            // frame against a wall left the pinwheels shivering there.
            Vec2 n;
            e.pos = keepOut (e.pos, e.radius, &n);
            if (n.lengthSq() > 0.0f)
            {
                const float into = dot (e.vel, n);
                if (into < 0.0f) e.vel -= n * (into * 1.6f);
                const Vec2 heading = fromAngle (e.wander);
                const float hIn = dot (heading, n);
                if (hIn < 0.0f) e.wander = (heading - n * (2.0f * hIn)).angle();
            }
        }

        // Keep the crowd from stacking into one blob: a light shove apart.
        for (size_t i = 0; i < enemies.size(); ++i)
        {
            auto& a = enemies[i];
            if (! a.alive || a.spawn > 0.0f || a.kind == EnemyKind::well) continue;
            for (size_t j = i + 1; j < enemies.size(); ++j)
            {
                auto& b = enemies[j];
                if (! b.alive || b.spawn > 0.0f || b.kind == EnemyKind::well) continue;
                const Vec2 d = a.pos - b.pos;
                const float minD = (a.radius + b.radius) * 0.9f;
                const float dd = d.lengthSq();
                if (dd < minD * minD && dd > 0.01f)
                {
                    const float l = std::sqrt (dd);
                    const Vec2 push = d / l * ((minD - l) * 0.5f);
                    a.pos += push; b.pos -= push;
                }
            }
        }
    }

    //==========================================================================
    // Towers and the gate

    void World::updateTowers (float dt)
    {
        for (auto& t : towers)
        {
            t.flash = std::max (0.0f, t.flash - dt * 5.0f);
            if (! t.alive) t.fall = std::min (1.0f, t.fall + dt * 0.8f);
            if (t.active && t.rise < 1.0f)
            {
                t.rise = std::min (1.0f, t.rise + dt / kRiseSeconds);
                if (rng.chance (dt * 30.0f))
                    explode (t.pos + rng.inDisc (t.radius), zoneColor (level, t.zone), { 1, 1, 1 }, 4, 300.0f, 0.6f, 1.4f);
            }
        }
    }

    void World::destroyTower (Tower& t, int chord)
    {
        if (! t.alive) return;
        t.alive = false;
        t.fall = 0.0f;

        const Color c = zoneColor (level, t.zone);
        explode (t.pos, c, { 1, 1, 1 }, 420, 1500.0f, 2.2f, 1.1f);
        explode (t.pos, c, chordColor (chord), 200, 800.0f, 1.6f, 1.6f);
        grid.push (t.pos, 3000.0f, 700.0f);
        rings.push_back ({ t.pos, t.radius, 1800.0f, 0.9f, c });
        rings.push_back ({ t.pos, t.radius, 1100.0f, 1.2f, { 1, 1, 1 } });
        shakeAmount = std::max (shakeAmount, 20.0f);
        flashAmount = std::max (flashAmount, 0.7f);

        // What stood round it goes with it, for the score.
        for (auto& e : enemies)
            if (e.alive && (e.pos - t.pos).length() < 650.0f) kill (e, chord, true, false);

        addScore (t.wave == 0 ? 5000 : 10000, t.pos);
        band.post ({ Command::tower, chord, 0, panOf (t.pos) });

        // Its door, of its zone and wave, goes with it.
        auto& door = doors[(size_t) (t.wave * 3 + t.zone)];
        door.bars = { false, false, false };
        if (! door.open)
        {
            door.open = true;
            const int d = t.wave * 3 + t.zone;
            lvl.openDoor (d);
            grid.holdWhere ([this] (Vec2 p) { return lvl.solid (p); });
            const Vec2 at = lvl.doorRect (d).centre();
            rings.push_back ({ at, 40.0f, 1200.0f, 0.8f, c });
            explode (at, c, { 1, 1, 1 }, 160, 900.0f, 1.4f, 1.2f);
            const int left = doorsLeft();
            if (left > 0)
                say (left == 1 ? "A DOOR OPENS  ·  1 LEFT" : "A DOOR OPENS  ·  " + std::to_string (left) + " LEFT", 2.2f, c);
        }

        // The first three down: the second three rise, one in every zone.
        if (t.wave == 0)
        {
            bool waveDown = true;
            for (const auto& o : towers) if (o.wave == 0 && o.alive) waveDown = false;
            if (waveDown)
            {
                for (auto& o : towers)
                    if (o.wave == 1 && ! o.active)
                    {
                        o.active = true;
                        o.rise = 0.0f;
                        rings.push_back ({ o.pos, o.radius, 900.0f, 0.7f, zoneColor (level, o.zone) });
                        grid.push (o.pos, 1200.0f, 400.0f);
                    }
                say ("THREE MORE TOWERS", 2.4f, levelAccent (level));
            }
        }
    }

    void World::updateGate (float dt)
    {
        if (! gate.open)
        {
            if (state == Phase::playing && doorsLeft() == 0)
            {
                gate.open = true;
                gate.openTime = 0.0f;
                say ("THE EXIT IS OPEN", 3.0f, levelAccent (level));
                rings.push_back ({ gate.pos, 20.0f, 900.0f, 1.0f, levelAccent (level) });
                band.post ({ Command::tower, band.pubChord.load(), 1, panOf (gate.pos) });
            }
            return;
        }

        gate.openTime += dt;
        if (state == Phase::playing && ship.alive && gate.openTime > 1.0f && (ship.pos - gate.pos).length() < 80.0f)
        {
            state = Phase::cleared;
            clearedTime = 0.0f;
            band.firing = false;
            setSlow (false);
            for (auto& en : enemies) if (en.alive) kill (en, 0, false, false);
            points += lives * 5000 + bombs * 2000;
            band.post ({ Command::finish });
            say (level >= 2 ? "ALL CLEAR" : "LEVEL CLEAR", 1.0e9f, levelAccent (level));
            for (int i = 0; i < 6; ++i)
                explode (gate.pos + rng.inDisc (400.0f), chordColor (i), levelAccent (level), 140, 900.0f, 2.0f, 1.2f);
            rings.push_back ({ gate.pos, 10.0f, 1600.0f, 1.2f, { 1, 1, 1 } });
        }
    }

    //==========================================================================

    void World::updateBullets (float dt)
    {
        for (auto& b : bullets)
        {
            if (! b.alive) continue;

            // Wells bend shots.
            for (const auto& e : enemies)
            {
                if (! e.alive || e.kind != EnemyKind::well || e.spawn > 0.0f) continue;
                const Vec2 d = e.pos - b.pos;
                const float dist = d.length();
                if (dist < 300.0f && dist > 1.0f) b.vel += d / dist * (2600.0f * (1.0f - dist / 300.0f) * dt);
            }

            const Vec2 before = b.pos;
            b.pos += b.vel * dt;
            b.life -= dt;

            if (lvl.solid (b.pos) || b.life <= 0.0f)
            {
                b.alive = false;
                for (int i = 0; i < 4; ++i)
                {
                    Particle p;
                    p.pos = before;
                    p.vel = rng.inDisc (380.0f) - b.vel.normalised() * 120.0f;
                    p.color = chordColor (b.chord).withAlpha (0.8f);
                    p.life = p.maxLife = rng.range (0.15f, 0.35f);
                    p.width = 2.0f;
                    p.z = 16.0f;
                    p.vz = rng.range (0.0f, 200.0f);
                    particles.push_back (p);
                }
            }
        }
    }

    void World::updateParticles (float dt)
    {
        const float step = dt * 60.0f;

        // Wells drag light in.
        Vec2 wellPos[4]; int wellCount = 0;
        for (const auto& e : enemies)
            if (e.alive && e.kind == EnemyKind::well && e.spawn <= 0.0f && wellCount < 4) wellPos[wellCount++] = e.pos;

        for (auto& p : particles)
        {
            p.life -= dt;
            p.vel *= std::pow (p.drag, step);

            for (int w = 0; w < wellCount; ++w)
            {
                const Vec2 d = wellPos[w] - p.pos;
                const float dist = d.length();
                if (dist < 300.0f && dist > 4.0f)
                    p.vel += (d / dist * 2200.0f + d.perp() / dist * 900.0f) * ((1.0f - dist / 300.0f) * dt);
            }

            const Vec2 before = p.pos;
            p.pos += p.vel * dt;

            // Up and down: gravity, a bounce off the floor that keeps 40%.
            p.vz -= kSparkGravity * dt;
            p.vz *= std::pow (p.drag, step * 0.5f);
            p.z += p.vz * dt;
            if (p.z < 0.0f) { p.z = -p.z * 0.4f; p.vz = std::abs (p.vz) * 0.4f; }

            // Walls: bounce off the side that was hit. Above a wall's top a
            // spark flies over it.
            if (p.z < Level::kWallHeight && lvl.solid (p.pos))
            {
                if (lvl.solid ({ p.pos.x, before.y })) p.vel.x = -p.vel.x * 0.6f;
                if (lvl.solid ({ before.x, p.pos.y })) p.vel.y = -p.vel.y * 0.6f;
                p.pos = before;
            }
        }

        particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                         particles.end());

        // Past the cap the oldest go first; a frame with ten thousand sparks is
        // a frame that drops.
        if (particles.size() > 7000) particles.erase (particles.begin(), particles.begin() + (particles.size() - 7000));
    }

    void World::updatePickups (float dt)
    {
        for (auto& p : pickups)
        {
            p.life -= dt;
            p.angle += dt * 2.0f;
            p.vel *= std::exp (-dt * 0.8f);

            if (ship.alive)
            {
                const Vec2 d = ship.pos - p.pos;
                const float dist = d.length();
                if (dist < 160.0f && dist > 1.0f) p.vel += d / dist * (1400.0f * dt);   // a little magnet
                if (dist < 38.0f)
                {
                    p.alive = false;
                    band.post ({ Command::powerUp });
                    static const char* names[3] { "ARP 1/8", "ARP 1/16", "ARP 1/32" };
                    const int rate = std::min (2, band.pubArpRate.load() + 1);
                    popups.push_back ({ p.pos, names[rate], 1.4f, kPowerUpColor, 30.0f });
                    explode (p.pos, kPowerUpColor, { 1, 1, 1 }, 60, 600.0f, 0.9f);
                    rings.push_back ({ p.pos, 20.0f, 900.0f, 0.5f, kPowerUpColor });
                }
            }

            p.pos = keepOut (p.pos + p.vel * dt, 20.0f);
            if (p.life <= 0.0f) p.alive = false;
        }
    }

    //==========================================================================

    void World::collide()
    {
        for (auto& b : bullets)
        {
            if (! b.alive) continue;

            // Into a tower: it takes the hit and every quarter of its strength
            // gone is heard as a chord.
            for (auto& t : towers)
            {
                if (! t.solidNow()) continue;
                const float r = t.radius + 6.0f;
                if ((t.pos - b.pos).lengthSq() > r * r) continue;

                b.alive = false;
                if (! t.shootable()) break;   // still rising: the shot just stops
                const int quarterBefore = (int) std::ceil (t.hp / t.maxHp * 4.0f);
                t.hp -= 1.0f;
                t.flash = 1.0f;
                const int quarterAfter = (int) std::ceil (t.hp / t.maxHp * 4.0f);
                for (int i = 0; i < 3; ++i)
                {
                    Particle p;
                    p.pos = b.pos;
                    p.vel = rng.inDisc (420.0f) - b.vel.normalised() * 200.0f;
                    p.color = mix (zoneColor (level, t.zone), chordColor (b.chord), rng.unit()).withAlpha (0.9f);
                    p.life = p.maxLife = rng.range (0.2f, 0.5f);
                    p.width = 2.2f;
                    p.z = rng.range (10.0f, 120.0f);
                    p.vz = rng.range (0.0f, 300.0f);
                    particles.push_back (p);
                }
                grid.push (t.pos, 40.0f, 200.0f);
                if (t.hp <= 0.0f) destroyTower (t, b.chord);
                else if (quarterAfter < quarterBefore)
                {
                    band.post ({ Command::kill, b.chord, 2, panOf (t.pos), -1, (int) EnemyKind::well });
                    rings.push_back ({ t.pos, t.radius, 700.0f, 0.5f, zoneColor (level, t.zone) });
                    shakeAmount = std::max (shakeAmount, 6.0f);
                }
                break;
            }
            if (! b.alive) continue;

            for (auto& e : enemies)
            {
                if (! e.alive || e.spawn > 0.0f) continue;
                const float r = e.radius + 6.0f;
                if ((e.pos - b.pos).lengthSq() > r * r) continue;

                b.alive = false;

                if (e.kind == EnemyKind::well)
                {
                    e.hp -= 1.0f;
                    e.flash = 1.0f;
                    e.vel += b.vel * 0.01f;
                    grid.push (e.pos, 120.0f, 120.0f);
                    if (e.hp <= 0.0f) kill (e, b.chord, true, true, b.note);
                }
                else
                {
                    kill (e, b.chord, true, true, b.note);
                }
                break;
            }
        }

        // Enemies into the ship.
        if (state == Phase::playing && ship.alive && ship.invulnerable <= 0.0f)
        {
            for (const auto& e : enemies)
            {
                if (! e.alive || e.spawn > 0.0f) continue;
                const float r = e.radius * 0.8f + 12.0f;
                if ((e.pos - ship.pos).lengthSq() < r * r) { shipDies(); break; }
            }
        }
    }

    void World::kill (Enemy& e, int chord, bool scored, bool sounded, int note)
    {
        if (! e.alive) return;
        e.alive = false;

        const Color ec = enemyColor (e.kind);
        const Color cc = chordColor (chord);
        const bool big = e.kind == EnemyKind::well;

        explode (e.pos, ec, cc, big ? 220 : 70, big ? 1100.0f : 650.0f, big ? 1.8f : 1.1f);
        grid.push (e.pos, big ? 1800.0f : 520.0f, big ? 460.0f : 150.0f);
        if (big) { shakeAmount = std::max (shakeAmount, 10.0f); rings.push_back ({ e.pos, 30.0f, 1400.0f, 0.6f, ec }); }

        if (sounded)
            band.post ({ Command::kill, chord, chordSize (e.kind), panOf (e.pos), note, (int) e.kind });

        if (scored)
        {
            ++killsSinceDeath;
            multiplier = std::min (10, 1 + killsSinceDeath / 30);
            addScore (e.value * multiplier, e.pos);
            if (! slowActive) slowMeter = std::min (1.0f, slowMeter + kSlowPerKill);   // no refill while it runs

            // Arpeggio power-ups drop now and then while there is room to speed up.
            if (band.pubArpRate.load() < 2 && pickups.empty() && rng.chance (big ? 0.6f : 0.022f))
                pickups.push_back ({ e.pos, rng.inDisc (80.0f), 9.0f, 0.0f, true });
        }

        if (e.kind == EnemyKind::splitter && scored)
            for (int i = 0; i < 3; ++i)
            {
                spawnEnemy (EnemyKind::shard, e.pos + fromAngle (kTau * i / 3.0f + e.angle, 24.0f), 0.0f);
                if (! incoming.empty()) incoming.back().vel = fromAngle (kTau * i / 3.0f + e.angle, 320.0f);
            }
    }

    void World::explode (Vec2 pos, Color a, Color b, int count, float speed, float life, float up)
    {
        for (int i = 0; i < count; ++i)
        {
            Particle p;
            p.pos = pos;
            const float s = speed * (0.15f + 0.85f * std::pow (rng.unit(), 0.6f));
            p.vel = fromAngle (rng.range (0.0f, kTau), s);
            p.color = mix (a, b, rng.unit());
            p.color.a = rng.range (0.6f, 1.0f);
            p.life = p.maxLife = life * rng.range (0.4f, 1.0f);
            p.width = rng.range (1.8f, 3.4f);
            p.drag = rng.range (0.92f, 0.965f);
            p.z = 12.0f;
            p.vz = s * rng.range (0.1f, up);
            particles.push_back (p);
        }
    }

    void World::addScore (int amount, Vec2 where)
    {
        points += amount;
        if (amount >= 200) popups.push_back ({ where, std::to_string (amount), 0.8f, { 1, 1, 1 }, amount >= 10000 ? 34.0f : 18.0f });

        if (points >= nextLifeAt) { ++lives; nextLifeAt += kLifeEvery; say ("+1 LIFE", 1.6f, { 0.4f, 1.0f, 0.5f }); }
        if (points >= nextBombAt) { ++bombs; nextBombAt += kBombEvery; say ("+1 BOMB", 1.6f, kBombColor); }
    }

    void World::shipDies()
    {
        ship.alive = false;
        band.firing = false;
        setSlow (false);
        band.post ({ Command::death });

        explode (ship.pos, { 1, 1, 1 }, chordColor (band.pubChord.load()), 500, 1300.0f, 2.2f);
        explode (ship.pos, { 1.0f, 0.8f, 0.3f }, { 1.0f, 0.2f, 0.1f }, 200, 700.0f, 1.6f);
        grid.push (ship.pos, 2600.0f, 600.0f);
        rings.push_back ({ ship.pos, 20.0f, 1600.0f, 0.8f, { 1, 1, 1 } });
        shakeAmount = 22.0f;
        flashAmount = 0.6f;

        // Everything on the level goes with it, silently and for no score.
        for (auto& e : enemies) if (e.alive) kill (e, 0, false, false);
        pickups.clear();

        multiplier = 1;
        killsSinceDeath = 0;

        if (--lives <= 0)
        {
            state = Phase::gameOver;
            band.post ({ Command::stop });
            say ("GAME OVER", 1.0e9f, { 1.0f, 0.25f, 0.25f });
        }
        else
        {
            state = Phase::dead;
            deadTime = 0.0f;
        }
    }

    void World::respawn()
    {
        if (state != Phase::dead) return;
        state = Phase::playing;
        ship.alive = true;
        ship.pos = keepOut (ship.pos, 30.0f);   // where it died: the level was cleared with it
        ship.vel = {};
        ship.invulnerable = 2.0f;
        rings.push_back ({ ship.pos, 300.0f, -500.0f, 0.55f, chordColor (band.pubChord.load()) });
    }

    void World::detonateBomb()
    {
        if (bombs <= 0 || ! ship.alive) return;
        --bombs;
        band.post ({ Command::bomb });

        for (auto& e : enemies)
            if (e.alive && (e.pos - ship.pos).length() < kBombRadius) kill (e, band.pubChord.load(), false, false);
        grid.push (ship.pos, 3200.0f, 1400.0f);
        rings.push_back ({ ship.pos, 30.0f, 2200.0f, 0.9f, kBombColor });
        rings.push_back ({ ship.pos, 10.0f, 1600.0f, 1.1f, { 1, 1, 1 } });
        explode (ship.pos, kBombColor, { 1.0f, 0.6f, 0.5f }, 400, 1800.0f, 1.6f);
        shakeAmount = 18.0f;
        flashAmount = 0.8f;
        ship.invulnerable = std::max (ship.invulnerable, 1.0f);
    }

    void World::threats (std::vector<Threat>& out) const
    {
        out.clear();
        for (const auto& e : enemies)
            if (e.alive) out.push_back ({ e.pos, e.vel, e.radius, e.spawn <= 0.0f });
    }
}
