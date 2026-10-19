#include "band_stream.h"

namespace godot
{

Ref<AudioStreamPlayback> BandStream::_instantiate_playback() const
{
    Ref<BandPlayback> p;
    p.instantiate();
    p->band = band;
    return p;
}

void BandPlayback::_start (double)
{
    playing = true;
}

void BandPlayback::_stop()
{
    playing = false;
}

int32_t BandPlayback::_mix (AudioFrame* buffer, float, int32_t frames)
{
    if (! playing || ! band || ! band->prepared.load())
    {
        for (int32_t i = 0; i < frames; ++i)
            buffer[i] = { 0.0f, 0.0f };
        return frames;
    }

    if ((int32_t) left.size() < frames)
    {
        // Only on the first mixes, or if Godot ever asks for more at once.
        left.resize ((size_t) frames);
        right.resize ((size_t) frames);
    }

    band->conductor.render (left.data(), right.data(), frames);

    const bool record = band->recordOn.load();
    for (int32_t i = 0; i < frames; ++i)
    {
        buffer[i].left = left[(size_t) i];
        buffer[i].right = right[(size_t) i];
        if (record && band->recording.size() + 2 <= band->recording.capacity())
        {
            band->recording.push_back (left[(size_t) i]);
            band->recording.push_back (right[(size_t) i]);
        }
    }
    return frames;
}

} // namespace godot
