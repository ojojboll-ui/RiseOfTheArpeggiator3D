#pragma once

// Neonpuls: copied from breakout/Source/audio/Ensemble.h on 2026-10-07. Only the namespace
// changed (now np::audio); if it is fixed in one place, fix it in the other.

#include <algorithm>
#include <cmath>
#include <vector>

/*
    Borrowed from Chordtroller (chordtroller/Source/dsp/Ensemble.h) on
    2026-09-16, for the asteroid chord. Unchanged apart from the namespace; if
    it is fixed in one place, fix it in the other.
*/
namespace np::audio
{

/**
    The chorus, which is most of the sound.

    An Omnichord's strings are not remarkable on their own — pulses with a fast
    decay. What makes the instrument recognisable across a room is the ensemble
    smeared over them, and getting that wrong means everything else can be right
    and it still will not sound like one.

    The design is Scriptor's Chorus, which is the Juno's, and four details carry
    it:

      - **One LFO, not two.** The width comes from reading the same sweep at two
        points half a cycle apart, so the channels are always exactly opposed
        rather than wandering independently. Two LFOs give width too, and a
        drifting image with it.
      - **Short delay.** Roughly 1 to 6 ms. Longer is a vibrato you hear as
        pitch; shorter is a flanger.
      - **Slow, deep sweep.** Around half a hertz. The depth is what makes it
        audible, the slowness is what keeps it from seasickness.
      - **No feedback.** A bucket-brigade chorus is a plain delay plus dry. Add
        feedback and it is a flanger, which is a different pedal.

    Scriptor draws its sweep on ten faders and needs a curve interpolator for it.
    We do not expose that, so the triangle is generated and the dependency is
    gone. One gentle lowpass in the wet path stands in for the bucket brigade's
    own dullness.
*/
class Ensemble
{
public:
    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn;

        buffer.assign ((size_t) (sampleRateIn * 0.05) + 4, 0.0f);   // 50 ms is ample
        reset();

        // One pole at about 7 kHz: a bucket brigade is not a clean delay, and
        // the wet path being a shade duller than the dry is half of why the
        // effect reads as depth rather than as detuning.
        lowpassCoefficient = 1.0f - std::exp (-2.0f * kPi * 7000.0f / (float) sampleRateIn);
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePosition = 0;
        phase = 0.0;
        lowpassL = lowpassR = 0.0f;
    }

    void setRate (float hz) noexcept { rateHz = std::clamp (hz, 0.05f, 6.0f); }

    /** How much of the wet is heard. Half is where a bucket-brigade chorus is
        widest, because dry and wet cancel and reinforce in equal measure. */
    void setMix (float mix01) noexcept { mix = std::clamp (mix01, 0.0f, 1.0f); }

    void process (float in, float& outL, float& outR) noexcept
    {
        if (buffer.empty())
        {
            outL = outR = in;
            return;
        }

        buffer[(size_t) writePosition] = in;

        phase += rateHz / sampleRate;
        if (phase >= 1.0)
            phase -= 1.0;

        // The same sweep, read half a cycle apart. This is the width.
        const float wetL = tap (kBaseMs + triangle (phase)        * kSweepMs);
        const float wetR = tap (kBaseMs + triangle (phase + 0.5)  * kSweepMs);

        lowpassL += lowpassCoefficient * (wetL - lowpassL);
        lowpassR += lowpassCoefficient * (wetR - lowpassR);

        outL = in * (1.0f - mix) + lowpassL * mix;
        outR = in * (1.0f - mix) + lowpassR * mix;

        if (++writePosition >= (int) buffer.size())
            writePosition = 0;
    }

private:
    static constexpr float  kPi      = 3.14159265358979f;
    static constexpr float  kBaseMs  = 1.2f;
    static constexpr float  kSweepMs = 4.8f;

    static float triangle (double p) noexcept
    {
        p -= std::floor (p);
        return (float) (1.0 - std::abs (2.0 * p - 1.0));
    }

    /** Linear interpolation, which is right here rather than lazy: the line is
        swept continuously, and a higher-order interpolator would ring on the
        modulation itself. */
    float tap (float milliseconds) const noexcept
    {
        const double size = (double) buffer.size();
        double position = (double) writePosition - (double) milliseconds * 0.001 * sampleRate;

        while (position < 0.0)
            position += size;

        const int first  = (int) position;
        const int second = (first + 1) % (int) size;
        const float frac = (float) (position - first);

        return buffer[(size_t) first] * (1.0f - frac) + buffer[(size_t) second] * frac;
    }

    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int    writePosition = 0;
    double phase  = 0.0;
    float  rateHz = 0.5f;
    float  mix    = 0.5f;

    float lowpassL = 0.0f, lowpassR = 0.0f, lowpassCoefficient = 0.5f;
};

} // namespace np::audio
