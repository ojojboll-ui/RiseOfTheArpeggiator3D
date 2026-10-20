#pragma once

#include <string>
#include <vector>

#include "../audio/Conductor.h"
#include "Grid.h"
#include "Level.h"
#include "Math.h"

/**
    The level, everything on it, and the rules.

    The world never decides *when* anything musical happens. It reads the band
    (bar, beat, layer, chord) and reacts: a Shot event from the arpeggio is
    when a bullet leaves the ship, a Bar event is when a wave arrives, the end
    of a death's drop is when the ship comes back. What it sends the other way
    is intent — which chord the left stick points at, whether the right one is
    out, what died where, and which zone the ship is in.

    In the 3D game (2026-10-19) the world still lives in a plane, x right and
    y down, in centimetres, and draws nothing; Godot reads what is where and
    stands it up. A level is three rooms (Level.h), each a section of the song.

    Since 2026-10-20 the towers come in two waves of three, one in each room
    and its colour: the first take half the hits, and when all three are down
    the second three rise, as strong as the towers were before. Every tower
    felled takes away one of the six doors in the exit corridor at the top,
    the door of its zone and wave. With all six gone the gate at the top
    opens; fly into it for the next song.
*/
namespace np
{
    constexpr float kShipSpeed   = 560.0f;
    constexpr float kBulletSpeed = 1500.0f;

    struct Controls
    {
        Vec2 move;              // -1..1, deadzone already taken out, +y down
        Vec2 aim;               // direction and how far out
        bool fire = false;
        bool bomb = false;      // pressed this frame
        bool slow = false;      // slow motion, held
    };

    enum class EnemyKind { wanderer, seeker, dodger, splitter, shard, well };
    constexpr int kEnemyKinds = 6;

    /** Three difficulties (2026-10-09). Normal is the game as it was tuned.
        `spawn` scales every wave's size, `speed` every enemy's, and the
        half-bar waves start at `halfBarLayer`. */
    struct Difficulty { const char* name; float spawn, speed; int lives, bombs, halfBarLayer; };

    inline const Difficulty& difficulty (int index)
    {
        static const Difficulty d[3] {
            { "EASY",   0.60f, 0.85f, 5, 4, 4 },
            { "NORMAL", 1.00f, 1.00f, 3, 3, 2 },
            { "HARD",   1.45f, 1.15f, 3, 2, 1 } };
        return d[index < 0 ? 0 : (index > 2 ? 2 : index)];
    }

    class World
    {
    public:
        enum class Phase { playing, dead, gameOver, cleared };

        /** What a run takes from one level to the next. */
        struct Carry { int score = 0, lives = 0, bombs = 0; };

        explicit World (audio::Conductor& bandIn, std::uint32_t seed = 0x2f6b9a1du);

        void start (int level, int difficultyIndex = 1, const Carry* carry = nullptr);
        int difficultyIndex() const { return diff; }
        void update (float dt, const Controls& in);
        void onEvent (const audio::Event& e);
        Carry carry() const { return { points, lives, bombs }; }

        /** Floor and sparks only — for the menu's background, round the
            verse room's start. */
        void updateAmbient (float dt);

        Phase phase() const { return state; }
        int score() const { return points; }
        int levelIndex() const { return level; }
        Vec2 shake() const { return shakeVec; }
        float flash() const { return flashAmount; }

        /** For the autopilot: what is on the field. */
        struct Threat { Vec2 pos, vel; float radius; bool live; };
        void threats (std::vector<Threat>& out) const;
        Vec2 shipPos() const { return ship.pos; }
        bool shipAlive() const { return ship.alive; }

        /** 0 at full speed, 1 at full slow motion, eased like the clock. */
        float slowAmount() const { return std::clamp ((1.0f - timeScale) / (1.0f - kSlowSpeed), 0.0f, 1.0f); }
        int bombsLeft() const { return bombs; }

        struct Ship
        {
            Vec2 pos, vel;
            float angle = -kPi * 0.5f;
            float invulnerable = 0.0f;
            bool alive = true;
        };

        struct Enemy
        {
            EnemyKind kind = EnemyKind::seeker;
            Vec2 pos, vel;
            float angle = 0.0f, spin = 0.0f, radius = 20.0f;
            float spawn = 0.0f;         // seconds left of spawning in, harmless meanwhile
            float hp = 1.0f, wander = 0.0f, flash = 0.0f, eaten = 0.0f;
            int value = 50;
            bool alive = true;
        };

        /** A zone's tower: shoot it down. Towers send out enemies while the
            ship is in their room. A second-wave tower is inactive — under the
            floor — until the first wave of its zone is down, then rises. */
        struct Tower
        {
            Vec2 pos;
            int zone = 0, wave = 0, slot = 0;
            float hp = 1.0f, maxHp = 1.0f, flash = 0.0f, radius = 62.0f, fall = 0.0f, rise = 1.0f;
            bool alive = true, active = true;
            bool solidNow() const { return alive && active; }
            bool shootable() const { return alive && active && rise >= 1.0f; }
        };
        static constexpr int kTowers = 6;

        /** A door in the exit corridor, drawn as three bars; it goes when the
            tower of its zone and wave falls. */
        struct Door { int zone = 0, wave = 0; std::array<bool, 3> bars { true, true, true }; bool open = false; };

        struct Gate { Vec2 pos; bool open = false; float openTime = 0.0f; };

        struct Bullet { Vec2 pos, vel; int chord = 0, note = -1; float life = 1.5f; bool accent = false; bool alive = true; };

        /** A spark. It flies in the plane like the 2D game's and, now that the
            floor is a floor, up as well: z is its height above the floor, and
            gravity brings it down to bounce. */
        struct Particle
        {
            Vec2 pos, vel;
            Color color;
            float life = 1.0f, maxLife = 1.0f, width = 2.4f, drag = 0.96f;
            float z = 0.0f, vz = 0.0f;
        };

        struct Pickup { Vec2 pos, vel; float life = 9.0f, angle = 0.0f; bool alive = true; };
        struct Popup { Vec2 pos; std::string text; float life = 1.0f; Color color; float size = 22.0f; };
        struct Ring  { Vec2 pos; float radius = 0.0f, speed = 1500.0f, life = 1.0f; Color color; };

        // What is where, for drawing -------------------------------------------
        const Ship& shipState() const { return ship; }
        const std::vector<Enemy>& enemyList() const { return enemies; }
        const std::vector<Bullet>& bulletList() const { return bullets; }
        const std::vector<Particle>& particleList() const { return particles; }
        const std::vector<Pickup>& pickupList() const { return pickups; }
        const std::vector<Popup>& popupList() const { return popups; }
        const std::vector<Ring>& ringList() const { return rings; }
        const std::array<Tower, kTowers>& towerList() const { return towers; }
        const std::array<Door, Level::kDoors>& doorList() const { return doors; }
        int doorsLeft() const;
        const Gate& gateState() const { return gate; }
        const Grid& floor() const { return grid; }
        const Level& ground() const { return lvl; }
        Vec2 aim() const { return aimDir; }
        int zone() const { return shipZone; }
        int towersLeft() const;
        bool finished() const { return state == Phase::cleared && level >= 2; }

        /** The ground's height under a point: hills and the floor's waves. */
        float heightAt (Vec2 p) const { return lvl.terrain (p) + grid.heightAt (p); }

        int lifeCount() const { return lives; }
        int multiplierValue() const { return multiplier; }
        float slowMeterValue() const { return slowMeter; }
        bool slowRunning() const { return slowActive; }
        float slowProgressValue() const { return slowProgress; }
        float kick() const { return kickPulse; }
        float chordFlashValue() const { return chordFlash; }
        float clock() const { return time; }
        const std::string& bannerText() const { return banner; }
        float bannerTime() const { return bannerLife; }
        Color bannerTint() const { return bannerColor; }

        static Color chordColor (int slot);
        static Color enemyColor (EnemyKind k);

    private:
        // Rules ------------------------------------------------------------------
        void updateShip (float dt, const Controls& in);
        void updateChordRequest (Vec2 move);
        void updateZone();
        void updateEnemies (float dt);
        void updateTowers (float dt);
        void updateGate (float dt);
        void updateBullets (float dt);
        void updateParticles (float dt);
        void updatePickups (float dt);
        void collide();

        void spawnShot (int note, int chord, int index, bool accent);
        void spawnWave (int bar, bool half);
        void towerBurst (Tower& t);
        void spawnEnemy (EnemyKind kind, Vec2 pos, float delay = 0.0f);
        Vec2 farPoint (float minDist);
        Vec2 chaseDirection (Vec2 from) const;
        Vec2 keepOut (Vec2 p, float radius, Vec2* normal = nullptr) const;
        void kill (Enemy& e, int chord, bool scored, bool sounded, int note = -1);
        void destroyTower (Tower& t, int chord);
        void explode (Vec2 pos, Color a, Color b, int count, float speed, float life, float up = 0.75f);
        void shipDies();
        void respawn();
        void detonateBomb();
        void addScore (int amount, Vec2 where);
        void say (const std::string& text, float life, Color color);
        void placeTowers();

        /** Where a sound sits left to right: measured from the ship, since the
            camera follows it. */
        float panOf (Vec2 p) const { return std::clamp (0.5f + (p.x - ship.pos.x) / 1800.0f, 0.0f, 1.0f); }

        audio::Conductor& band;
        Rng rng;
        Level lvl;
        Grid grid;
        Level::Flow chase;
        float chaseTimer = 0.0f;

        Phase state = Phase::playing;
        int level = 0, diff = 1;
        Ship ship;
        std::vector<Enemy> enemies;
        std::vector<Enemy> incoming;    // born this frame; joined after the loops that iterate enemies
        std::vector<Bullet> bullets;
        std::vector<Particle> particles;
        std::vector<Pickup> pickups;
        std::vector<Popup> popups;
        std::vector<Ring> rings;
        std::array<Tower, kTowers> towers {};
        std::array<Door, Level::kDoors> doors {};
        Gate gate;
        int shipZone = 0, lastWaveZone = 0;

        int points = 0, lives = 3, bombs = 3, multiplier = 1, killsSinceDeath = 0;
        static constexpr int kLifeEvery = 150000, kBombEvery = 120000;
        int nextLifeAt = kLifeEvery, nextBombAt = kBombEvery;
        Vec2 aimDir { 0.0f, -1.0f };
        int wheelSlot = 0;              // what the stick points at, with hysteresis
        float deadTime = 0.0f, time = 0.0f, clearedTime = 0.0f;
        int lastKicks = 0, lastHalfBars = 0;

        // Slow motion: a meter that drains while it is held and refills with
        // kills; while it runs everything moves at kSlowSpeed (2026-10-10).
        static constexpr float kSlowSpeed = 0.3f, kSlowDrain = 1.0f / 5.0f, kSlowPerKill = 0.04f;
        static constexpr float kSlowShipSpeed = kSlowSpeed;   // 0.55 for a day; now as slow as the rest, it is faster to begin with
        float slowMeter = 1.0f, timeScale = 1.0f;
        bool slowActive = false;
        float slowStartMeter = 1.0f, slowProgress = 0.0f, sentSlowDepth = 0.0f;   // how far through this slow motion's meter
        static constexpr float kSlowDip = 0.3f;   // where the slow filter lands at once: ~6 kHz
        void setSlow (bool on);
        float kickPulse = 0.0f, chordFlash = 0.0f, flashAmount = 0.0f;
        float shakeAmount = 0.0f;
        Vec2 shakeVec;
        std::string banner; float bannerLife = 0.0f; Color bannerColor;
    };


    /** Power-ups are green and bombs red, so the two never look alike. */
    constexpr Color kPowerUpColor { 0.35f, 1.0f, 0.45f };
    constexpr Color kBombColor    { 1.0f, 0.22f, 0.18f };

    /** Per-level looks. */
    Color levelTint (int level);
    Color levelAccent (int level);

    /** The three zones' colours on a level: the floor's grid, the tower. */
    Color zoneColor (int level, int zone);
}
