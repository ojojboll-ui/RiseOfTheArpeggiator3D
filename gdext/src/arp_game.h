#pragma once

#include "band_stream.h"
#include "game/Autopilot.h"
#include "game/World.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <memory>

namespace godot
{

/**
    The game, for Godot: the band, the rules and the autopilot from the 2D
    game, unchanged in what they do, behind one node.

    GDScript calls step() once a frame with the controls and draws what the
    accessors hand back. The drawing data comes as MultiMesh buffers (12
    floats of transform, 4 of colour, 4 of custom data per instance), built
    here because the world's units and its floor are here: one unit of the
    2D field is one centimetre, the field's centre is the origin, its y is
    Godot's z and the floor's waves are Godot's y.
*/
class ArpGame : public Node
{
    GDCLASS (ArpGame, Node)

public:
    ArpGame();
    ~ArpGame() override;

    Ref<AudioStream> get_stream();

    void start (int level, int difficulty, bool autopilot);
    void stop();
    void pause_band();
    void resume_band();
    void blip (int note);
    bool is_running() const { return world != nullptr; }
    void end_game();

    void step (double dt, Vector2 move, Vector2 aim, bool fire, bool bomb, bool slow);
    void ambient_step (double dt);

    int get_phase() const;
    int get_score() const;
    int get_level() const;
    bool get_autopilot() const { return autopilot; }
    void set_pilot_flags (bool reckless, bool slow_test);

    /** One frame's worth of everything the HUD shows. */
    Dictionary hud() const;
    Dictionary song_info (int index) const;
    PackedStringArray difficulty_names() const;

    /** Each buffer holds `capacity` instances, the unused ones zeroed, so
        the MultiMesh never has to be resized; last_count() says how many are
        real, for visible_instance_count. */
    PackedFloat32Array enemy_buffer (int kind, int capacity) const;
    PackedFloat32Array bullet_buffer (int capacity) const;
    PackedFloat32Array spark_buffer (int capacity) const;
    PackedFloat32Array pickup_buffer (int capacity) const;
    PackedFloat32Array ring_buffer (int capacity) const;
    int last_count() const { return lastCount; }
    Transform3D ship_transform() const;
    bool ship_visible() const;
    Vector3 ship_position() const;
    Vector3 aim_direction() const;

    PackedByteArray floor_bytes() const;
    int floor_columns() const;
    int floor_rows() const;
    Vector2 field_size() const;
    float floor_height (Vector3 at) const;

    Vector3 to_3d (Vector2 p) const;
    Vector2 from_3d (Vector3 p) const;

    bool take_died();
    bool take_bombed();

    void record (bool on, double max_seconds);
    bool save_wav (String path) const;
    int mix_rate() const;

protected:
    static void _bind_methods();

private:
    np::World* view() const { return world ? world.get() : ambient.get(); }
    Vector3 place (np::Vec2 p, float height) const;

    std::shared_ptr<Band> band;
    Ref<BandStream> stream;
    std::unique_ptr<np::World> world, ambient;
    np::Autopilot pilot;
    bool autopilot = false;
    bool died = false, bombed = false;
    float lastAngle = 0.0f, bank = 0.0f;
    mutable int lastCount = 0;
};

} // namespace godot
