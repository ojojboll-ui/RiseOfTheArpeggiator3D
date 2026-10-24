#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Math.h"

/**
    A level's ground: three rooms, the passages between them, the walls in
    them and the hills under them (2026-10-19).

    Each room is a zone and a section of the song: the verse, where the ship
    starts, the chorus and the bridge. They lie in a triangle, each joined to
    the other two by a passage narrower than the rooms, so the zones are
    clearly apart and there is always a way round. The verse has hills and
    valleys, the chorus walls, the bridge both — as asked.

    Everything here is in the game's units (a centimetre each), x right and y
    down, the map's top-left corner at the origin. Collision is a grid of
    20-unit cells: open floor, the void outside the rooms, or a wall. The
    hills are a coarser grid of heights cut into triangles, so they come out
    faceted, lowpoly, and the game and the floor shader agree on every height.

    The way out is in a room in the middle of the three (2026-10-24): the
    exit at its centre inside six round doors of different sizes, two in
    each zone's colour. Each zone has six places for towers; the game uses
    one per wave.
*/
namespace np
{
    struct Rect
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;

        bool contains (Vec2 p, float margin = 0.0f) const
        {
            return p.x >= x0 + margin && p.x <= x1 - margin && p.y >= y0 + margin && p.y <= y1 - margin;
        }
        Vec2 centre() const { return { (x0 + x1) * 0.5f, (y0 + y1) * 0.5f }; }
        Vec2 at (float u, float v) const { return { lerp (x0, x1, u), lerp (y0, y1, v) }; }
        float width() const { return x1 - x0; }
        float height() const { return y1 - y0; }
    };

    class Level
    {
    public:
        static constexpr float kCell = 20.0f;          // collision
        static constexpr float kFlowCell = 40.0f;      // enemies' way-finding
        static constexpr float kHillCell = 120.0f;     // terrain facets (80 before the level grew)
        static constexpr float kScale = 1.5f;          // the layout, half as big again (2026-10-24)
        static constexpr float kHillLift = 2.0f;       // hills twice as tall: steeper than before though wider
        static constexpr float kWallHeight = 80.0f;    // interior walls, for drawing
        static constexpr float kRimHeight = 50.0f;     // the rooms' edges

        enum Cell : std::uint8_t { open = 0, voidCell = 1, wall = 2, door = 3 };
        static constexpr int kDoors = 6;
        static constexpr int kTowerSpots = 6;          // per zone: three for each wave

        struct Room { Rect r; int zone; bool hills, walls; };
        struct Segment { Vec2 a, b, outward; };        // outward: into the void

        void build (int levelIndex);

        float width() const { return w; }
        float height() const { return h; }
        const std::array<Room, 3>& rooms() const { return roomList; }
        const std::vector<Rect>& passages() const { return passageList; }
        const std::vector<Rect>& walls() const { return wallList; }
        const std::vector<Segment>& rim() const { return rimList; }
        Vec2 start() const { return startPos; }
        Vec2 gate() const { return gatePos; }
        /** Where a zone's towers stand: spots 0-2 the first wave, 3-5 the second. */
        Vec2 towerSpot (int zone, int spot) const { return towerSpots[(size_t) zone][(size_t) spot]; }

        /** The exit corridor and its doors, bottom (nearest the chorus) to top.
            Door d is in zone d % 3's colour. */
        /** The middle room, and the radius of each round door round the exit
            at its centre, outermost first. */
        const Rect& hub() const { return hubRect; }
        float doorRadius (int d) const { return doorRadii[(size_t) d]; }
        static constexpr float kDoorThickness = 60.0f;
        bool doorOpen (int d) const { return doorIsOpen[(size_t) d]; }
        void openDoor (int d);
        /** Counts every change to what is solid, so way-finding knows to look again. */
        int revision() const { return rev; }

        /** The zone a point is in, or -1 in a passage or outside. */
        int zoneAt (Vec2 p) const
        {
            for (const auto& r : roomList) if (r.r.contains (p)) return r.zone;
            return -1;
        }
        const Rect& roomRect (int zone) const { return roomList[(size_t) std::clamp (zone, 0, 2)].r; }

        // Collision ---------------------------------------------------------------
        Cell cellAt (Vec2 p) const
        {
            const int c = (int) std::floor (p.x / kCell), r = (int) std::floor (p.y / kCell);
            if (c < 0 || r < 0 || c >= cols || r >= rows) return voidCell;
            return cells[(size_t) (r * cols + c)];
        }
        bool solid (Vec2 p) const { return cellAt (p) != open; }

        /** The nearest point to `want` with nothing solid within `clearance`. */
        Vec2 placeClear (Vec2 want, float clearance) const;

        /** Pushes a circle out of everything solid; `normal` gets the direction
            it was pushed, zero if it was not. */
        Vec2 resolve (Vec2 p, float radius, Vec2* normal = nullptr) const;

        /** Whether a straight line between two points stays on open floor. */
        bool lineOfSight (Vec2 a, Vec2 b) const;

        /** A random open point in a zone, at least `clearance` from anything solid. */
        Vec2 randomPoint (int zone, Rng& rng, float clearance) const;

        // Hills ---------------------------------------------------------------------
        /** The ground's height (up is positive) and its slope, rise per unit. */
        float terrain (Vec2 p) const;
        Vec2 slope (Vec2 p) const;
        const std::vector<float>& hillHeights() const { return hills; }
        int hillColumns() const { return hcols; }
        int hillRows() const { return hrows; }

        // Way-finding -----------------------------------------------------------------
        /** Distances to a target over open floor, by 40-unit cells. */
        struct Flow { std::vector<float> dist; int target = -1, revision = -1; };
        void updateFlow (Flow& f, Vec2 target) const;
        /** Which way to go from `from` along a flow field; zero if lost. */
        Vec2 flowDirection (const Flow& f, Vec2 from) const;

        /** For drawing the floor: 20-unit cells as RGBA, rgb the zone's colour
            (blended along the passages), a 255 on open floor. */
        std::vector<std::uint8_t> floorInfo (const std::array<Color, 3>& zoneColors, Color hubColor) const;
        int columns() const { return cols; }
        int rowCount() const { return rows; }

    private:
        void carve (const Rect& r, Cell type);
        void addHill (const Room& room, Vec2 c, float radius, float height);
        void buildRim();
        bool flowOpen (int c, int r) const;

        float w = 0, h = 0;
        int cols = 0, rows = 0;
        std::vector<Cell> cells;
        std::array<Room, 3> roomList {};
        std::vector<Rect> passageList, wallList;
        std::vector<Segment> rimList;
        std::array<std::array<Vec2, kTowerSpots>, 3> towerSpots {};
        Vec2 startPos, gatePos;
        Rect hubRect;
        std::array<float, kDoors> doorRadii {};
        void carveRing (float radius, Cell type);
        std::array<bool, kDoors> doorIsOpen {};
        int rev = 0;

        int hcols = 0, hrows = 0;
        std::vector<float> hills;

        int fcols = 0, frows = 0;
    };
}
