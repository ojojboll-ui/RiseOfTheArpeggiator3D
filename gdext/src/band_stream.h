#pragma once

#include "audio/Conductor.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace godot
{

/** The band and what the audio thread needs beside it. Shared between the
    game node, which talks to the band, and the playback, which renders it. */
struct Band
{
    np::audio::Conductor conductor;

    // An optional recording of everything played, for tests (--wav). The
    // buffer is reserved before recording starts, so the audio thread only
    // writes into memory it already has.
    std::vector<float> recording;
    std::atomic<bool> recordOn { false };
    std::atomic<bool> prepared { false };
};

/**
    The band as an AudioStream: put it on an AudioStreamPlayer and play it.
    The Conductor renders on Godot's mixing thread, so its sample clock is
    the clock the game's music runs on, as it was in the SDL game.
*/
class BandStream : public AudioStream
{
    GDCLASS (BandStream, AudioStream)

public:
    Ref<AudioStreamPlayback> _instantiate_playback() const override;
    String _get_stream_name() const override { return "Band"; }
    double _get_length() const override { return 0.0; }
    bool _is_monophonic() const override { return true; }

    std::shared_ptr<Band> band;

protected:
    static void _bind_methods() {}
};

class BandPlayback : public AudioStreamPlayback
{
    GDCLASS (BandPlayback, AudioStreamPlayback)

public:
    void _start (double from_pos) override;
    void _stop() override;
    bool _is_playing() const override { return playing; }
    int32_t _mix (AudioFrame* buffer, float rate_scale, int32_t frames) override;
    double _get_playback_position() const override { return 0.0; }

    std::shared_ptr<Band> band;

protected:
    static void _bind_methods() {}

private:
    std::vector<float> left, right;
    bool playing = false;
};

} // namespace godot
