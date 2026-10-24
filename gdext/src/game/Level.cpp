#include "Level.h"

#include <cmath>
#include <limits>
#include <queue>

namespace np
{
    namespace
    {
        float smooth (float e0, float e1, float x)
        {
            const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /** Distance from a point to a rectangle, zero inside. */
        float distanceTo (const Rect& r, Vec2 p)
        {
            const float dx = std::max ({ r.x0 - p.x, 0.0f, p.x - r.x1 });
            const float dy = std::max ({ r.y0 - p.y, 0.0f, p.y - r.y1 });
            return std::sqrt (dx * dx + dy * dy);
        }

        /** A wall of size w x h centred on a point of a room, in room fractions. */
        Rect block (const Rect& room, float u, float v, float wIn, float hIn)
        {
            const Vec2 c = room.at (u, v);
            return { c.x - wIn * 0.5f, c.y - hIn * 0.5f, c.x + wIn * 0.5f, c.y + hIn * 0.5f };
        }

        struct HillSpec { float u, v, radius, height; };
    }

    //==========================================================================

    void Level::build (int levelIndex)
    {
        const int variant = ((levelIndex % 3) + 3) % 3;

        // The exit room in the middle (2026-10-24, "det rummet är i mitten
        // av de tre andra"), the chorus above it, the verse below left and
        // the bridge below right, each joined to it by a passage; the rooms
        // meet only there. Laid out in the old units and made half as big
        // again (kScale, 2026-10-24): rooms 33 x 28.5 m, the middle 36 m
        // square, passages 6 m wide.
        auto R = [] (float x0, float y0, float x1, float y1) { return Rect { x0 * kScale, y0 * kScale, x1 * kScale, y1 * kScale }; };
        w = 7400.0f * kScale;
        h = 6000.0f * kScale;
        hubRect = R (2500.0f, 2300.0f, 4900.0f, 4700.0f);
        roomList[1] = { R (2600.0f, 200.0f,  4800.0f, 2100.0f), 1, false, true  };   // chorus: walls
        roomList[0] = { R (200.0f,  3900.0f, 2400.0f, 5800.0f), 0, true,  false };   // verse: hills
        roomList[2] = { R (5000.0f, 3900.0f, 7200.0f, 5800.0f), 2, true,  true  };   // bridge: both

        passageList = {
            R (3500.0f, 2100.0f, 3900.0f, 2300.0f),    // chorus - middle
            R (2400.0f, 4100.0f, 2500.0f, 4500.0f),    // verse - middle
            R (4900.0f, 4100.0f, 5000.0f, 4500.0f),    // middle - bridge
            hubRect };

        const Rect& verse = roomList[0].r;
        const Rect& chorus = roomList[1].r;
        const Rect& bridge = roomList[2].r;

        // The exit at the centre of the middle room, inside six round doors
        // of different sizes, outermost first: the first wave's verse,
        // chorus and bridge doors, then the second wave's.
        gatePos = hubRect.centre();
        for (int d = 0; d < kDoors; ++d)
        {
            doorRadii[(size_t) d] = (960.0f - 140.0f * (float) d) * kScale;
            doorIsOpen[(size_t) d] = false;
        }

        // Walls: a layout per level.
        wallList.clear();
        switch (variant)
        {
            case 0:
                for (float u : { 0.30f, 0.70f })
                    for (float v : { 0.33f, 0.67f })
                        wallList.push_back (block (chorus, u, v, 200.0f * kScale, 200.0f * kScale));
                wallList.push_back (block (bridge, 0.32f, 0.42f, 90.0f * kScale, 700.0f * kScale));
                wallList.push_back (block (bridge, 0.78f, 0.60f, 600.0f * kScale, 90.0f * kScale));
                break;

            case 1:
                wallList.push_back (block (chorus, 0.5f, 0.30f, 800.0f * kScale, 90.0f * kScale));
                wallList.push_back (block (chorus, 0.5f, 0.70f, 800.0f * kScale, 90.0f * kScale));
                wallList.push_back (block (chorus, 0.12f, 0.5f, 90.0f * kScale, 500.0f * kScale));
                wallList.push_back (block (chorus, 0.88f, 0.5f, 90.0f * kScale, 500.0f * kScale));
                wallList.push_back (block (bridge, 0.32f, 0.30f, 600.0f * kScale, 90.0f * kScale));
                wallList.push_back (block (bridge, 0.32f, 0.70f, 600.0f * kScale, 90.0f * kScale));
                break;

            default:
                for (float su : { -1.0f, 1.0f })
                    for (float sv : { -1.0f, 1.0f })
                    {
                        const Vec2 c = chorus.centre() + Vec2 { su * 460.0f, sv * 420.0f } * kScale;
                        const float arm = 320.0f * kScale, half = 45.0f * kScale;
                        wallList.push_back ({ std::min (c.x, c.x - su * arm), c.y - half, std::max (c.x, c.x - su * arm), c.y + half });
                        wallList.push_back ({ c.x - half, std::min (c.y, c.y - sv * arm), c.x + half, std::max (c.y, c.y - sv * arm) });
                    }
                // A fortress in the bridge, open to the right.
                wallList.push_back (block (bridge, 0.47f, 0.5f, 90.0f * kScale, 620.0f * kScale));
                wallList.push_back (block (bridge, 0.60f, 0.33f, 480.0f * kScale, 90.0f * kScale));
                wallList.push_back (block (bridge, 0.60f, 0.67f, 480.0f * kScale, 90.0f * kScale));
                break;
        }

        // Collision cells.
        cols = (int) std::ceil (w / kCell);
        rows = (int) std::ceil (h / kCell);
        cells.assign ((size_t) (cols * rows), voidCell);
        for (const auto& r : roomList) carve (r.r, open);
        for (const auto& p : passageList) carve (p, open);
        for (const auto& wl : wallList) carve (wl, wall);
        for (int d = 0; d < kDoors; ++d) carveRing (doorRadii[(size_t) d], door);
        buildRim();
        ++rev;

        // Where everything stands, kept clear of the walls: the start in the
        // verse, and six tower spots per room — the first wave's three, then
        // the second's.
        startPos = placeClear (verse.at (0.62f, 0.55f), 80.0f);
        const float spots[3][kTowerSpots][2] {
            { { 0.22f, 0.28f }, { 0.75f, 0.25f }, { 0.25f, 0.78f },  { 0.48f, 0.15f }, { 0.85f, 0.78f }, { 0.12f, 0.52f } },
            { { 0.50f, 0.50f }, { 0.18f, 0.50f }, { 0.82f, 0.50f },  { 0.50f, 0.18f }, { 0.30f, 0.82f }, { 0.70f, 0.82f } },
            { { 0.62f, 0.50f }, { 0.25f, 0.18f }, { 0.85f, 0.22f },  { 0.30f, 0.85f }, { 0.88f, 0.80f }, { 0.60f, 0.12f } } };
        for (int z = 0; z < 3; ++z)
            for (int k = 0; k < kTowerSpots; ++k)
                towerSpots[(size_t) z][(size_t) k] = placeClear (roomList[(size_t) z].r.at (spots[z][k][0], spots[z][k][1]), 130.0f);

        // Hills: bumps and hollows, mirrored left to right on every other level.
        hcols = (int) std::ceil (w / kHillCell) + 1;
        hrows = (int) std::ceil (h / kHillCell) + 1;
        hills.assign ((size_t) (hcols * hrows), 0.0f);
        const bool mirror = variant == 1;
        const HillSpec verseHills[] {
            { 0.30f, 0.55f, 520.0f, 170.0f }, { 0.65f, 0.20f, 440.0f, 140.0f },
            { 0.45f, 0.35f, 380.0f, -110.0f }, { 0.10f, 0.30f, 320.0f, 100.0f },
            { 0.80f, 0.55f, 360.0f, -95.0f }, { 0.55f, 0.85f, 340.0f, 120.0f } };
        const HillSpec bridgeHills[] {
            { 0.20f, 0.55f, 460.0f, 160.0f }, { 0.42f, 0.10f, 360.0f, -100.0f },
            { 0.75f, 0.40f, 360.0f, 130.0f }, { 0.70f, 0.92f, 320.0f, -85.0f } };
        // Bigger since 2026-10-24: as wide as the rooms grew, and taller still,
        // so the slopes are steeper and the ship and enemies feel them.
        for (const auto& hs : verseHills)
            addHill (roomList[0], verse.at (mirror ? 1.0f - hs.u : hs.u, hs.v), hs.radius * kScale, hs.height * kHillLift);
        for (const auto& hs : bridgeHills)
            addHill (roomList[2], bridge.at (mirror ? 1.0f - hs.u : hs.u, hs.v), hs.radius * kScale, hs.height * kHillLift);

        fcols = (int) std::ceil (w / kFlowCell);
        frows = (int) std::ceil (h / kFlowCell);
    }

    void Level::openDoor (int d)
    {
        if (d < 0 || d >= kDoors || doorIsOpen[(size_t) d]) return;
        doorIsOpen[(size_t) d] = true;
        carveRing (doorRadii[(size_t) d], open);
        ++rev;
    }

    void Level::carveRing (float radius, Cell type)
    {
        const float half = kDoorThickness * 0.5f;
        const int c0 = std::max (0, (int) ((gatePos.x - radius - half) / kCell) - 1);
        const int c1 = std::min (cols - 1, (int) ((gatePos.x + radius + half) / kCell) + 1);
        const int r0 = std::max (0, (int) ((gatePos.y - radius - half) / kCell) - 1);
        const int r1 = std::min (rows - 1, (int) ((gatePos.y + radius + half) / kCell) + 1);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
            {
                const Vec2 p { ((float) c + 0.5f) * kCell, ((float) r + 0.5f) * kCell };
                if (std::abs ((p - gatePos).length() - radius) > half) continue;
                auto& cell = cells[(size_t) (r * cols + c)];
                if (cell == voidCell || cell == wall) continue;
                cell = type;
            }
    }

    Vec2 Level::placeClear (Vec2 want, float clearance) const
    {
        auto clear = [&] (Vec2 p)
        {
            if (solid (p)) return false;
            for (int k = 0; k < 12; ++k)
                if (solid (p + fromAngle (kTau * (float) k / 12.0f, clearance))) return false;
            return true;
        };
        if (clear (want)) return want;
        for (float r = 20.0f; r < 800.0f; r += 20.0f)
            for (int k = 0; k < 24; ++k)
            {
                const Vec2 p = want + fromAngle (kTau * (float) k / 24.0f, r);
                if (clear (p)) return p;
            }
        return want;
    }

    void Level::carve (const Rect& r, Cell type)
    {
        const int c0 = std::max (0, (int) std::floor (r.x0 / kCell + 0.001f));
        const int c1 = std::min (cols, (int) std::ceil (r.x1 / kCell - 0.001f));
        const int r0 = std::max (0, (int) std::floor (r.y0 / kCell + 0.001f));
        const int r1 = std::min (rows, (int) std::ceil (r.y1 / kCell - 0.001f));
        for (int y = r0; y < r1; ++y)
            for (int x = c0; x < c1; ++x)
                cells[(size_t) (y * cols + x)] = type;
    }

    void Level::addHill (const Room& room, Vec2 c, float radius, float height)
    {
        for (int r = 0; r < hrows; ++r)
            for (int x = 0; x < hcols; ++x)
            {
                const Vec2 p { (float) x * kHillCell, (float) r * kHillCell };
                const float d = (p - c).length();
                if (d >= radius) continue;

                // Flat along the room's edges, under the passages' mouths, and
                // round the start, the gate and the tower, so those stand level.
                const float edge = std::min ({ p.x - room.r.x0, room.r.x1 - p.x, p.y - room.r.y0, room.r.y1 - p.y });
                float fade = smooth (90.0f, 390.0f, edge);
                fade *= smooth (180.0f, 380.0f, (p - startPos).length());
                for (const Vec2 flat : towerSpots[(size_t) room.zone])
                    fade *= smooth (160.0f, 340.0f, (p - flat).length());
                if (fade <= 0.0f) continue;

                const float k = 1.0f - (d / radius) * (d / radius);
                hills[(size_t) (r * hcols + x)] += height * k * k * fade;
            }
    }

    void Level::buildRim()
    {
        rimList.clear();
        // Doors stand on floor: the corridor's edges run past them.
        auto at = [this] (int c, int r) -> Cell
        {
            if (c < 0 || r < 0 || c >= cols || r >= rows) return voidCell;
            const Cell cell = cells[(size_t) (r * cols + c)];
            return cell == door ? open : cell;
        };

        // Horizontal edges between rows r-1 and r, merged into runs.
        for (int r = 0; r <= rows; ++r)
        {
            int runStart = -1, runDir = 0;
            for (int c = 0; c <= cols; ++c)
            {
                int dir = 0;
                if (c < cols)
                {
                    const Cell above = at (c, r - 1), below = at (c, r);
                    if (above == open && below == voidCell) dir = 1;          // void below
                    else if (above == voidCell && below == open) dir = -1;    // void above
                }
                if (dir != runDir)
                {
                    if (runDir != 0)
                        rimList.push_back ({ { (float) runStart * kCell, (float) r * kCell }, { (float) c * kCell, (float) r * kCell },
                                             { 0.0f, (float) runDir } });
                    runStart = c;
                    runDir = dir;
                }
            }
        }

        // Vertical edges between columns c-1 and c.
        for (int c = 0; c <= cols; ++c)
        {
            int runStart = -1, runDir = 0;
            for (int r = 0; r <= rows; ++r)
            {
                int dir = 0;
                if (r < rows)
                {
                    const Cell left = at (c - 1, r), right = at (c, r);
                    if (left == open && right == voidCell) dir = 1;
                    else if (left == voidCell && right == open) dir = -1;
                }
                if (dir != runDir)
                {
                    if (runDir != 0)
                        rimList.push_back ({ { (float) c * kCell, (float) runStart * kCell }, { (float) c * kCell, (float) r * kCell },
                                             { (float) runDir, 0.0f } });
                    runStart = r;
                    runDir = dir;
                }
            }
        }
    }

    //==========================================================================

    Vec2 Level::resolve (Vec2 p, float radius, Vec2* normal) const
    {
        Vec2 q = p;
        for (int pass = 0; pass < 2; ++pass)
        {
            const int c0 = (int) std::floor ((q.x - radius) / kCell), c1 = (int) std::floor ((q.x + radius) / kCell);
            const int r0 = (int) std::floor ((q.y - radius) / kCell), r1 = (int) std::floor ((q.y + radius) / kCell);
            bool moved = false;

            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                {
                    const bool inside = c >= 0 && r >= 0 && c < cols && r < rows;
                    if (inside && cells[(size_t) (r * cols + c)] == open) continue;

                    const float bx0 = (float) c * kCell, by0 = (float) r * kCell;
                    const Vec2 closest { std::clamp (q.x, bx0, bx0 + kCell), std::clamp (q.y, by0, by0 + kCell) };
                    const Vec2 d = q - closest;
                    const float dist = d.length();
                    if (dist >= radius) continue;

                    if (dist > 1.0e-4f) q += d / dist * (radius - dist);
                    else
                    {
                        // The centre is inside the cell: out through its nearest side.
                        const float left = q.x - bx0, right = bx0 + kCell - q.x, top = q.y - by0, bottom = by0 + kCell - q.y;
                        const float m = std::min ({ left, right, top, bottom });
                        if (m == left) q.x = bx0 - radius;
                        else if (m == right) q.x = bx0 + kCell + radius;
                        else if (m == top) q.y = by0 - radius;
                        else q.y = by0 + kCell + radius;
                    }
                    moved = true;
                }
            if (! moved) break;
        }

        if (normal != nullptr) *normal = (q - p).normalised();
        return q;
    }

    bool Level::lineOfSight (Vec2 a, Vec2 b) const
    {
        const Vec2 d = b - a;
        const float len = d.length();
        const int steps = (int) (len / (kCell * 0.5f)) + 1;
        for (int i = 1; i < steps; ++i)
            if (solid (a + d * ((float) i / (float) steps))) return false;
        return true;
    }

    Vec2 Level::randomPoint (int zone, Rng& rng, float clearance) const
    {
        const Rect& r = roomRect (zone);
        for (int tries = 0; tries < 40; ++tries)
        {
            const Vec2 p { rng.range (r.x0 + clearance, r.x1 - clearance), rng.range (r.y0 + clearance, r.y1 - clearance) };
            bool clear = ! solid (p);
            for (int k = 0; k < 8 && clear; ++k) clear = ! solid (p + fromAngle (kTau * (float) k / 8.0f, clearance));
            if (clear) return p;
        }
        return r.centre();
    }

    //==========================================================================

    float Level::terrain (Vec2 p) const
    {
        const float fx = std::clamp (p.x / kHillCell, 0.0f, (float) (hcols - 1) - 0.001f);
        const float fy = std::clamp (p.y / kHillCell, 0.0f, (float) (hrows - 1) - 0.001f);
        const int c = (int) fx, r = (int) fy;
        const float tx = fx - (float) c, ty = fy - (float) r;
        const float h00 = hills[(size_t) (r * hcols + c)], h10 = hills[(size_t) (r * hcols + c + 1)];
        const float h01 = hills[(size_t) ((r + 1) * hcols + c)], h11 = hills[(size_t) ((r + 1) * hcols + c + 1)];
        // Each cell is two triangles split from its top-left to its bottom-right
        // corner — floor.gdshader cuts them the same way.
        if (tx >= ty) return h00 + tx * (h10 - h00) + ty * (h11 - h10);
        return h00 + ty * (h01 - h00) + tx * (h11 - h01);
    }

    Vec2 Level::slope (Vec2 p) const
    {
        const float fx = std::clamp (p.x / kHillCell, 0.0f, (float) (hcols - 1) - 0.001f);
        const float fy = std::clamp (p.y / kHillCell, 0.0f, (float) (hrows - 1) - 0.001f);
        const int c = (int) fx, r = (int) fy;
        const float tx = fx - (float) c, ty = fy - (float) r;
        const float h00 = hills[(size_t) (r * hcols + c)], h10 = hills[(size_t) (r * hcols + c + 1)];
        const float h01 = hills[(size_t) ((r + 1) * hcols + c)], h11 = hills[(size_t) ((r + 1) * hcols + c + 1)];
        if (tx >= ty) return Vec2 { h10 - h00, h11 - h10 } / kHillCell;
        return Vec2 { h11 - h01, h01 - h00 } / kHillCell;
    }

    //==========================================================================

    bool Level::flowOpen (int c, int r) const
    {
        if (c < 0 || r < 0 || c >= fcols || r >= frows) return false;
        // A flow cell is two collision cells square, and open only if all four are.
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
            {
                const int cc = c * 2 + x, rr = r * 2 + y;
                if (cc >= cols || rr >= rows || cells[(size_t) (rr * cols + cc)] != open) return false;
            }
        return true;
    }

    void Level::updateFlow (Flow& f, Vec2 target) const
    {
        int tc = (int) (target.x / kFlowCell), tr = (int) (target.y / kFlowCell);

        // A target against a wall: the nearest open cell round it.
        if (! flowOpen (tc, tr))
        {
            bool found = false;
            for (int ring = 1; ring <= 3 && ! found; ++ring)
                for (int dy = -ring; dy <= ring && ! found; ++dy)
                    for (int dx = -ring; dx <= ring && ! found; ++dx)
                        if (flowOpen (tc + dx, tr + dy)) { tc += dx; tr += dy; found = true; }
            if (! found) return;
        }

        const int index = tr * fcols + tc;
        if (index == f.target && f.revision == rev && ! f.dist.empty()) return;
        f.target = index;
        f.revision = rev;

        constexpr float inf = std::numeric_limits<float>::max();
        f.dist.assign ((size_t) (fcols * frows), inf);

        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        f.dist[(size_t) index] = 0.0f;
        queue.push ({ 0.0f, index });

        static constexpr int dx[8] { 1, -1, 0, 0, 1, 1, -1, -1 };
        static constexpr int dy[8] { 0, 0, 1, -1, 1, -1, 1, -1 };

        while (! queue.empty())
        {
            const auto [d, i] = queue.top();
            queue.pop();
            if (d > f.dist[(size_t) i]) continue;
            const int c = i % fcols, r = i / fcols;

            for (int k = 0; k < 8; ++k)
            {
                const int nc = c + dx[k], nr = r + dy[k];
                if (! flowOpen (nc, nr)) continue;
                // No cutting corners: a diagonal needs both sides open.
                if (k >= 4 && (! flowOpen (c + dx[k], r) || ! flowOpen (c, r + dy[k]))) continue;
                const float nd = d + (k >= 4 ? 1.41421f : 1.0f);
                const int ni = nr * fcols + nc;
                if (nd < f.dist[(size_t) ni])
                {
                    f.dist[(size_t) ni] = nd;
                    queue.push ({ nd, ni });
                }
            }
        }
    }

    Vec2 Level::flowDirection (const Flow& f, Vec2 from) const
    {
        if (f.dist.empty()) return {};
        const int c = (int) (from.x / kFlowCell), r = (int) (from.y / kFlowCell);
        if (c < 0 || r < 0 || c >= fcols || r >= frows) return {};

        float here = f.dist[(size_t) (r * fcols + c)];
        static constexpr int dx[8] { 1, -1, 0, 0, 1, 1, -1, -1 };
        static constexpr int dy[8] { 0, 0, 1, -1, 1, -1, 1, -1 };

        int best = -1;
        float bestD = here;
        for (int k = 0; k < 8; ++k)
        {
            const int nc = c + dx[k], nr = r + dy[k];
            if (! flowOpen (nc, nr)) continue;
            if (k >= 4 && (! flowOpen (c + dx[k], r) || ! flowOpen (c, r + dy[k]))) continue;
            const float d = f.dist[(size_t) (nr * fcols + nc)];
            if (d < bestD) { bestD = d; best = k; }
        }
        if (best < 0) return {};
        const Vec2 centre { ((float) (c + dx[best]) + 0.5f) * kFlowCell, ((float) (r + dy[best]) + 0.5f) * kFlowCell };
        return (centre - from).normalised();
    }

    //==========================================================================

    std::vector<std::uint8_t> Level::floorInfo (const std::array<Color, 3>& zoneColors, Color hubColor) const
    {
        std::vector<std::uint8_t> out ((size_t) (cols * rows * 4), 0);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const Cell cell = cells[(size_t) (r * cols + c)];
                if (cell == voidCell) continue;
                const Vec2 p { ((float) c + 0.5f) * kCell, ((float) r + 0.5f) * kCell };

                // Inside a room its colour, the middle its own; in a passage,
                // the room's colour fading into the middle's.
                Color col;
                const int zone = zoneAt (p);
                if (zone >= 0) col = zoneColors[(size_t) zone];
                else if (hubRect.contains (p)) col = hubColor;
                else
                {
                    int nearest = 0;
                    for (int z = 1; z < 3; ++z)
                        if (distanceTo (roomList[(size_t) z].r, p) < distanceTo (roomList[(size_t) nearest].r, p)) nearest = z;
                    const float dRoom = distanceTo (roomList[(size_t) nearest].r, p), dHub = distanceTo (hubRect, p);
                    col = mix (zoneColors[(size_t) nearest], hubColor, dRoom / std::max (1.0f, dRoom + dHub));
                }

                std::uint8_t* px = &out[(size_t) ((r * cols + c) * 4)];
                px[0] = (std::uint8_t) std::lround (std::clamp (col.r, 0.0f, 1.0f) * 255.0f);
                px[1] = (std::uint8_t) std::lround (std::clamp (col.g, 0.0f, 1.0f) * 255.0f);
                px[2] = (std::uint8_t) std::lround (std::clamp (col.b, 0.0f, 1.0f) * 255.0f);
                px[3] = cell == open ? 255 : 128;
            }
        return out;
    }
}
