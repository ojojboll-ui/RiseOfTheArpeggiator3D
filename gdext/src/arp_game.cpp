#include "arp_game.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cmath>
#include <cstring>

namespace godot
{

namespace
{
    using np::Vec2;

    constexpr float kScale = 0.01f;     // one unit of the 2D field is a centimetre
    const Vec2 kMid { (np::kFieldL + np::kFieldR) * 0.5f, (np::kFieldT + np::kFieldB) * 0.5f };

    // How high things ride above the floor, in field units.
    // Models are drawn this much bigger than the 2D game's outlines: seen
    // from an angle and in perspective, the 2D sizes read as specks.
    constexpr float kLook = 1.4f;

    constexpr float kShipHover = 16.0f, kBulletHover = 16.0f, kPickupHover = 26.0f, kRingHover = 3.0f;

    Color toColor (np::Color c) { return Color (c.r, c.g, c.b, c.a); }

    /** Fills a MultiMesh buffer: transform rows, colour, custom data. */
    struct Writer
    {
        PackedFloat32Array out;
        float* w = nullptr;
        int count = 0;

        int capacity = 0;

        explicit Writer (int capacityIn) : capacity (std::max (0, capacityIn))
        {
            out.resize ((int64_t) capacity * 20);
            w = out.ptrw();
            if (capacity > 0) std::memset (w, 0, sizeof (float) * (size_t) capacity * 20);
        }

        bool full() const { return count >= capacity; }

        void put (const Basis& b, const Vector3& o, const Color& c, const Color& custom)
        {
            if (full()) return;
            float* p = w + (size_t) count * 20;
            for (int r = 0; r < 3; ++r)
            {
                p[r * 4 + 0] = (float) b.rows[r].x;
                p[r * 4 + 1] = (float) b.rows[r].y;
                p[r * 4 + 2] = (float) b.rows[r].z;
                p[r * 4 + 3] = (float) o[r];
            }
            p[12] = c.r; p[13] = c.g; p[14] = c.b; p[15] = c.a;
            p[16] = custom.r; p[17] = custom.g; p[18] = custom.b; p[19] = custom.a;
            ++count;
        }

        PackedFloat32Array finish (int& counted) { counted = count; return out; }
    };

    /** A turn about the up axis that points local +x along a 2D angle. */
    Basis yaw (float angle) { return Basis (Vector3 (0, 1, 0), -angle); }
}

ArpGame::ArpGame()
    : band (std::make_shared<Band>())
{
    stream.instantiate();
    stream->band = band;

    AudioServer* audio = AudioServer::get_singleton();
    band->conductor.prepare (audio != nullptr ? audio->get_mix_rate() : 48000.0);
    band->prepared = true;

    ambient = std::make_unique<np::World> (band->conductor, (std::uint32_t) Time::get_singleton()->get_ticks_usec());
}

ArpGame::~ArpGame()
{
    band->conductor.post ({ np::audio::Command::stop });
}

Ref<AudioStream> ArpGame::get_stream()
{
    return stream;
}

//==============================================================================

void ArpGame::start (int level, int difficulty, bool autopilotIn)
{
    autopilot = autopilotIn;
    world = std::make_unique<np::World> (band->conductor, (std::uint32_t) Time::get_singleton()->get_ticks_usec());
    world->start (level, difficulty);
    const bool reckless = pilot.reckless, slowTest = pilot.slowTest;
    pilot = {};
    pilot.reckless = reckless;
    pilot.slowTest = slowTest;
    died = bombed = false;
    lastAngle = world->shipState().angle;
    bank = 0.0f;
}

void ArpGame::set_pilot_flags (bool reckless, bool slowTest)
{
    pilot.reckless = reckless;
    pilot.slowTest = slowTest;
}

void ArpGame::stop()          { band->conductor.post ({ np::audio::Command::stop }); }
void ArpGame::pause_band()    { band->conductor.post ({ np::audio::Command::pause }); }
void ArpGame::resume_band()   { band->conductor.post ({ np::audio::Command::resume }); }

void ArpGame::blip (int note)
{
    band->conductor.post ({ np::audio::Command::blip, note, 0, 0.5f });
}

void ArpGame::end_game()
{
    band->conductor.post ({ np::audio::Command::stop });
    world.reset();
    autopilot = false;
}

void ArpGame::step (double dtIn, Vector2 move, Vector2 aim, bool fire, bool bomb, bool slow)
{
    if (! world) return;
    auto& w = *world;
    const float dt = (float) std::min (dtIn, 1.0 / 20.0);

    np::audio::Event ev;
    while (band->conductor.poll (ev))
        w.onEvent (ev);

    np::Controls c;
    if (autopilot) c = pilot.think (w, dt);
    else
    {
        c.move = { (float) move.x, (float) move.y };
        c.aim = { (float) aim.x, (float) aim.y };
        c.fire = fire;
        c.bomb = bomb;
        c.slow = slow;
    }

    const auto phaseBefore = w.phase();
    const int bombsBefore = w.bombsLeft();

    // Substeps keep fast bullets from passing through small enemies.
    const int steps = std::max (1, (int) std::ceil (dt / (1.0f / 120.0f)));
    for (int s = 0; s < steps; ++s)
    {
        np::Controls one = c;
        if (s > 0) one.bomb = false;
        w.update (dt / (float) steps, one);
    }

    if (phaseBefore == np::World::Phase::playing && w.phase() != np::World::Phase::playing) died = true;
    if (w.bombsLeft() < bombsBefore) bombed = true;

    // The ship banks into its turns.
    const float a = w.shipState().angle;
    const float turn = np::angleDiff (a, lastAngle) / std::max (dt, 1.0e-4f);
    lastAngle = a;
    bank += (std::clamp (turn * 0.09f, -0.7f, 0.7f) - bank) * std::min (1.0f, dt * 10.0f);
}

void ArpGame::ambient_step (double dt)
{
    ambient->updateAmbient ((float) std::min (dt, 1.0 / 20.0));
}

int ArpGame::get_phase() const { return world ? (int) world->phase() : -1; }
int ArpGame::get_score() const { return world ? world->score() : 0; }
int ArpGame::get_level() const { return world ? world->levelIndex() : 0; }

bool ArpGame::take_died()   { const bool d = died;   died = false;   return d; }
bool ArpGame::take_bombed() { const bool b = bombed; bombed = false; return b; }

//==============================================================================

Dictionary ArpGame::hud() const
{
    Dictionary d;
    const auto& cond = band->conductor;
    const np::World* w = view();
    const int level = w->levelIndex();
    const auto& song = np::audio::song (level);

    d["score"] = w->score();
    d["multiplier"] = w->multiplierValue();
    d["lives"] = w->lifeCount();
    d["bombs"] = w->bombsLeft();
    d["slow_meter"] = w->slowMeterValue();
    d["slow_running"] = w->slowRunning();
    d["slow_amount"] = w->slowAmount();
    d["slow_progress"] = w->slowProgressValue();
    d["phase"] = world ? (int) world->phase() : -1;
    d["level"] = level;
    d["difficulty"] = String (np::difficulty (w->difficultyIndex()).name);
    d["song"] = String (song.name);
    d["progress"] = std::clamp ((float) (cond.beatPosition() / 4.0 / song.lengthBars), 0.0f, 1.0f);
    d["layer"] = cond.pubLayer.load();
    d["max_layer"] = np::audio::maxLayer (song);
    d["bars_to_layer"] = cond.pubBarsToLayer.load();
    d["arp_rate"] = cond.pubArpRate.load();
    d["rate_bars"] = cond.pubRateBars.load();
    d["arp_direction"] = song.randomArp ? String (np::audio::arpDirectionName (cond.pubArpDir.load())) : String();
    d["chord"] = cond.pubChord.load();
    d["wanted"] = cond.requestedChord.load();
    d["kick"] = w->kick();
    d["chord_flash"] = w->chordFlashValue();
    d["flash"] = w->flash();
    d["time"] = w->clock();
    d["shake"] = Vector2 (w->shake().x * kScale, w->shake().y * kScale);
    d["banner"] = String::utf8 (w->bannerText().c_str());
    d["banner_life"] = w->bannerTime();
    d["banner_color"] = toColor (w->bannerTint());
    d["accent"] = toColor (np::levelAccent (level));
    d["tint"] = toColor (np::levelTint (level));
    d["power_color"] = toColor (np::kPowerUpColor);
    d["bomb_color"] = toColor (np::kBombColor);

    PackedStringArray layers;
    for (int i = 0; i <= np::audio::maxLayer (song); ++i) layers.push_back (np::audio::layerName (song, i));
    d["layer_names"] = layers;

    PackedStringArray chords;
    Array colors;
    for (int s = 0; s < 4; ++s)
    {
        char name[8];
        np::audio::chordName (song, s, name, sizeof (name));
        chords.push_back (name);
        colors.push_back (toColor (np::World::chordColor (s)));
    }
    d["chord_names"] = chords;
    d["chord_colors"] = colors;

    Array popups;
    for (const auto& p : w->popupList())
    {
        Array one;
        one.push_back (place (p.pos, 40.0f));
        one.push_back (String::utf8 (p.text.c_str()));
        one.push_back (std::clamp (p.life * 2.0f, 0.0f, 1.0f));
        one.push_back (toColor (p.color));
        one.push_back (p.size);
        popups.push_back (one);
    }
    d["popups"] = popups;
    return d;
}

Dictionary ArpGame::song_info (int index) const
{
    const auto& s = np::audio::song (index);
    Dictionary d;
    d["name"] = String (s.name);
    d["tagline"] = String (s.tagline);
    d["bpm"] = (int) s.bpm;
    d["key_note"] = np::audio::placeAbove (s.keyRoot, 64);
    PackedStringArray chords;
    for (int k = 0; k < 4; ++k)
    {
        char name[8];
        np::audio::chordName (s, k, name, sizeof (name));
        chords.push_back (name);
    }
    d["chords"] = chords;
    d["accent"] = toColor (np::levelAccent (index));
    d["tint"] = toColor (np::levelTint (index));
    return d;
}

PackedStringArray ArpGame::difficulty_names() const
{
    PackedStringArray out;
    for (int i = 0; i < 3; ++i) out.push_back (np::difficulty (i).name);
    return out;
}

//==============================================================================

Vector3 ArpGame::place (Vec2 p, float height) const
{
    const float floor = view()->floor().heightAt (p);
    return Vector3 ((p.x - kMid.x) * kScale, (floor + height) * kScale, (p.y - kMid.y) * kScale);
}

Vector3 ArpGame::to_3d (Vector2 p) const
{
    return place ({ (float) p.x, (float) p.y }, 0.0f);
}

Vector2 ArpGame::from_3d (Vector3 p) const
{
    return Vector2 ((float) p.x / kScale + kMid.x, (float) p.z / kScale + kMid.y);
}

float ArpGame::floor_height (Vector3 at) const
{
    return view()->floor().heightAt ({ (float) at.x / kScale + kMid.x, (float) at.z / kScale + kMid.y }) * kScale;
}

PackedFloat32Array ArpGame::enemy_buffer (int kind, int capacity) const
{
    const auto& list = view()->enemyList();
    Writer out (capacity);
    const float kick = view()->kick();

    for (const auto& e : list)
    {
        if (! e.alive || (int) e.kind != kind) continue;

        float scale = 1.0f, alpha = 1.0f;
        if (e.spawn > 0.0f)
        {
            // Spawning in: comes down from large, flickering — visible, harmless.
            scale = 1.0f + e.spawn * 2.2f;
            alpha = std::fmod (e.spawn * 14.0f, 1.0f) > 0.4f ? 0.75f : 0.25f;
        }
        if (e.kind == np::EnemyKind::seeker) scale *= 1.0f + 0.18f * kick;

        np::Color c = np::World::enemyColor (e.kind);
        if (e.flash > 0.0f) c = np::mix (c, { 1, 1, 1 }, e.flash);

        const float r = e.radius * scale * kScale * kLook;
        const Basis b = yaw (e.angle).scaled (Vector3 (r, r, r));
        out.put (b, place (e.pos, e.radius * 0.9f + (scale - 1.0f) * 30.0f), Color (c.r, c.g, c.b, alpha),
                 Color (alpha, e.flash, kick, e.spawn));
    }
    return out.finish (lastCount);
}

PackedFloat32Array ArpGame::bullet_buffer (int capacity) const
{
    const auto& list = view()->bulletList();
    Writer out (capacity);

    for (const auto& b : list)
    {
        if (! b.alive) continue;
        const Vec2 dir = b.vel.normalised();
        const float len = (b.accent ? 26.0f : 18.0f) * kScale;
        const float thick = (b.accent ? 5.5f : 4.0f) * kScale;
        const np::Color c = np::World::chordColor (b.chord);

        Basis basis;
        basis.set_column (0, Vector3 (dir.x, 0, dir.y) * len);
        basis.set_column (1, Vector3 (0, thick, 0));
        basis.set_column (2, Vector3 (-dir.y, 0, dir.x) * thick);
        const Vector3 at = place (b.pos, kBulletHover) - Vector3 (dir.x, 0, dir.y) * (len * 0.5f);
        out.put (basis, at, Color (c.r, c.g, c.b, 1.0f), Color (b.accent ? 1.0f : 0.0f, 0, 0, 0));
    }
    return out.finish (lastCount);
}

PackedFloat32Array ArpGame::spark_buffer (int capacity) const
{
    const auto& list = view()->particleList();
    Writer out (capacity);

    for (const auto& p : list)
    {
        const float a = std::clamp (p.life / p.maxLife, 0.0f, 1.0f) * p.color.a;
        if (a <= 0.003f) continue;
        out.put (Basis(), place (p.pos, p.z), Color (p.color.r, p.color.g, p.color.b, a),
                 Color (p.vel.x * kScale, p.vz * kScale, p.vel.y * kScale, p.width * kScale));
    }
    return out.finish (lastCount);
}

PackedFloat32Array ArpGame::pickup_buffer (int capacity) const
{
    const auto& list = view()->pickupList();
    Writer out (capacity);
    const float kick = view()->kick();

    for (const auto& p : list)
    {
        if (! p.alive) continue;
        const float blink = p.life < 2.0f ? (std::sin (p.life * 25.0f) > 0.0f ? 1.0f : 0.25f) : 1.0f;
        const float r = 18.0f * kScale * kLook;
        const Basis b = (yaw (p.angle) * Basis (Vector3 (1, 0, 0), p.angle * 0.7f)).scaled (Vector3 (r, r, r));
        const np::Color c = np::kPowerUpColor;
        out.put (b, place (p.pos, kPickupHover), Color (c.r, c.g, c.b, blink), Color (blink, kick, 0, 0));
    }
    return out.finish (lastCount);
}

PackedFloat32Array ArpGame::ring_buffer (int capacity) const
{
    const auto& list = view()->ringList();
    Writer out (capacity);

    for (const auto& r : list)
    {
        if (r.radius <= 0.0f) continue;
        const float a = std::clamp (r.life * 1.5f, 0.0f, 1.0f);
        out.put (Basis(), place (r.pos, kRingHover), Color (r.color.r, r.color.g, r.color.b, a),
                 Color (r.radius * kScale, 6.0f * kScale, a, 0));
    }
    return out.finish (lastCount);
}

Transform3D ArpGame::ship_transform() const
{
    const auto& s = view()->shipState();
    const Basis b = yaw (s.angle) * Basis (Vector3 (1, 0, 0), bank);
    return Transform3D (b, place (s.pos, kShipHover));
}

bool ArpGame::ship_visible() const
{
    if (! world) return false;
    const auto& s = world->shipState();
    if (! s.alive) return false;
    return ! (s.invulnerable > 0.0f && std::fmod (s.invulnerable * 10.0f, 1.0f) < 0.35f);
}

Vector3 ArpGame::ship_position() const
{
    return place (view()->shipState().pos, kShipHover);
}

Vector3 ArpGame::aim_direction() const
{
    const Vec2 a = view()->aim();
    return Vector3 (a.x, 0, a.y);
}

PackedByteArray ArpGame::floor_bytes() const
{
    const auto& h = view()->floor().heights();
    PackedByteArray out;
    out.resize ((int64_t) (h.size() * sizeof (float)));
    float* w = reinterpret_cast<float*> (out.ptrw());
    for (size_t i = 0; i < h.size(); ++i) w[i] = h[i] * kScale;
    return out;
}

int ArpGame::floor_columns() const { return view()->floor().columns(); }
int ArpGame::floor_rows() const { return view()->floor().rowCount(); }

Vector2 ArpGame::field_size() const
{
    return Vector2 ((np::kFieldR - np::kFieldL) * kScale, (np::kFieldB - np::kFieldT) * kScale);
}

//==============================================================================

void ArpGame::record (bool on, double maxSeconds)
{
    if (on)
    {
        band->recordOn = false;
        band->recording.clear();
        band->recording.reserve ((size_t) (std::max (1.0, maxSeconds) * mix_rate() * 2.0));
        band->recordOn = true;
    }
    else band->recordOn = false;
}

int ArpGame::mix_rate() const
{
    AudioServer* audio = AudioServer::get_singleton();
    return audio != nullptr ? (int) audio->get_mix_rate() : 48000;
}

bool ArpGame::save_wav (String path) const
{
    band->recordOn = false;
    const auto& data = band->recording;
    Ref<FileAccess> f = FileAccess::open (path, FileAccess::WRITE);
    if (f.is_null()) return false;

    const int rate = mix_rate();
    const uint32_t bytes = (uint32_t) (data.size() * 2);
    f->store_buffer ((const uint8_t*) "RIFF", 4); f->store_32 (36 + bytes); f->store_buffer ((const uint8_t*) "WAVE", 4);
    f->store_buffer ((const uint8_t*) "fmt ", 4); f->store_32 (16); f->store_16 (1); f->store_16 (2);
    f->store_32 ((uint32_t) rate); f->store_32 ((uint32_t) rate * 4); f->store_16 (4); f->store_16 (16);
    f->store_buffer ((const uint8_t*) "data", 4); f->store_32 (bytes);
    for (float s : data) f->store_16 ((uint16_t) (int16_t) std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f));
    return true;
}

//==============================================================================

void ArpGame::_bind_methods()
{
    ClassDB::bind_method (D_METHOD ("get_stream"), &ArpGame::get_stream);
    ClassDB::bind_method (D_METHOD ("start", "level", "difficulty", "autopilot"), &ArpGame::start);
    ClassDB::bind_method (D_METHOD ("stop"), &ArpGame::stop);
    ClassDB::bind_method (D_METHOD ("pause_band"), &ArpGame::pause_band);
    ClassDB::bind_method (D_METHOD ("resume_band"), &ArpGame::resume_band);
    ClassDB::bind_method (D_METHOD ("blip", "note"), &ArpGame::blip);
    ClassDB::bind_method (D_METHOD ("is_running"), &ArpGame::is_running);
    ClassDB::bind_method (D_METHOD ("end_game"), &ArpGame::end_game);
    ClassDB::bind_method (D_METHOD ("step", "dt", "move", "aim", "fire", "bomb", "slow"), &ArpGame::step);
    ClassDB::bind_method (D_METHOD ("ambient_step", "dt"), &ArpGame::ambient_step);
    ClassDB::bind_method (D_METHOD ("get_phase"), &ArpGame::get_phase);
    ClassDB::bind_method (D_METHOD ("get_score"), &ArpGame::get_score);
    ClassDB::bind_method (D_METHOD ("get_level"), &ArpGame::get_level);
    ClassDB::bind_method (D_METHOD ("get_autopilot"), &ArpGame::get_autopilot);
    ClassDB::bind_method (D_METHOD ("set_pilot_flags", "reckless", "slow_test"), &ArpGame::set_pilot_flags);
    ClassDB::bind_method (D_METHOD ("hud"), &ArpGame::hud);
    ClassDB::bind_method (D_METHOD ("song_info", "index"), &ArpGame::song_info);
    ClassDB::bind_method (D_METHOD ("difficulty_names"), &ArpGame::difficulty_names);
    ClassDB::bind_method (D_METHOD ("enemy_buffer", "kind", "capacity"), &ArpGame::enemy_buffer);
    ClassDB::bind_method (D_METHOD ("bullet_buffer", "capacity"), &ArpGame::bullet_buffer);
    ClassDB::bind_method (D_METHOD ("spark_buffer", "capacity"), &ArpGame::spark_buffer);
    ClassDB::bind_method (D_METHOD ("pickup_buffer", "capacity"), &ArpGame::pickup_buffer);
    ClassDB::bind_method (D_METHOD ("ring_buffer", "capacity"), &ArpGame::ring_buffer);
    ClassDB::bind_method (D_METHOD ("last_count"), &ArpGame::last_count);
    ClassDB::bind_method (D_METHOD ("ship_transform"), &ArpGame::ship_transform);
    ClassDB::bind_method (D_METHOD ("ship_visible"), &ArpGame::ship_visible);
    ClassDB::bind_method (D_METHOD ("ship_position"), &ArpGame::ship_position);
    ClassDB::bind_method (D_METHOD ("aim_direction"), &ArpGame::aim_direction);
    ClassDB::bind_method (D_METHOD ("floor_bytes"), &ArpGame::floor_bytes);
    ClassDB::bind_method (D_METHOD ("floor_columns"), &ArpGame::floor_columns);
    ClassDB::bind_method (D_METHOD ("floor_rows"), &ArpGame::floor_rows);
    ClassDB::bind_method (D_METHOD ("field_size"), &ArpGame::field_size);
    ClassDB::bind_method (D_METHOD ("floor_height", "at"), &ArpGame::floor_height);
    ClassDB::bind_method (D_METHOD ("to_3d", "p"), &ArpGame::to_3d);
    ClassDB::bind_method (D_METHOD ("from_3d", "p"), &ArpGame::from_3d);
    ClassDB::bind_method (D_METHOD ("take_died"), &ArpGame::take_died);
    ClassDB::bind_method (D_METHOD ("take_bombed"), &ArpGame::take_bombed);
    ClassDB::bind_method (D_METHOD ("record", "on", "max_seconds"), &ArpGame::record);
    ClassDB::bind_method (D_METHOD ("save_wav", "path"), &ArpGame::save_wav);
    ClassDB::bind_method (D_METHOD ("mix_rate"), &ArpGame::mix_rate);
}

} // namespace godot
