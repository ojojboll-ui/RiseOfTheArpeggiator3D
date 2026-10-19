#pragma once

// Neonpuls: copied from chordtroller/Source/dsp/TempoDelay.h on 2026-10-07. Only the namespace
// changed (now np::audio); if it is fixed in one place, fix it in the other.

#include <algorithm>
#include <cmath>
#include <vector>

namespace np::audio
{

/**
    The J-6's delay: ping-pong, locked to the tempo, darker every time round.

    Half the synthwave and house sound is a dotted eighth bouncing between the
    speakers, so this is not decoration. Each repeat passes a lowpass on its way
    back into the loop, which is what a tape or a bucket brigade does and what
    stops the tail from turning into a wall of identical copies.

    Changing tempo moves the read point gradually rather than jumping it. The
    repeats bend in pitch for a moment, the way a tape delay does, instead of
    clicking.
*/
class TempoDelay
{
public:
    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn;
        left.assign ((size_t) (sampleRateIn * 2.5) + 4, 0.0f);
        right = left;
        reset();
    }

    void reset() noexcept
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        write = 0;
        lowL = lowR = 0.0f;
        currentDelay = targetDelay;
    }

    /** Division 0 eighth, 1 dotted eighth, 2 quarter. */
    void setTiming (double bpm, int division) noexcept
    {
        static constexpr double beats[3] { 0.5, 0.75, 1.0 };

        const double seconds = 60.0 / std::max (40.0, bpm) * beats[std::clamp (division, 0, 2)];
        targetDelay = (float) std::min (seconds * sampleRate, (double) left.size() - 4.0);
    }

    /** The time between repeats, in seconds, for whatever setTiming last set. */
    double delaySeconds() const noexcept { return (double) targetDelay / sampleRate; }

    void setAmount (float send, float feedbackIn) noexcept
    {
        sendLevel = std::clamp (send, 0.0f, 1.0f);
        feedback  = std::clamp (feedbackIn, 0.0f, 0.85f);
    }

    /** Adds the repeats onto the stereo pair in place. */
    void process (float& l, float& r) noexcept
    {
        if (left.empty())
            return;

        currentDelay += (targetDelay - currentDelay) * 0.0005f;

        const float wetL = read (left, currentDelay);
        const float wetR = read (right, currentDelay);

        lowL += 0.35f * (wetL - lowL);
        lowR += 0.35f * (wetR - lowR);

        const float input = (l + r) * 0.5f * sendLevel;

        // Ping-pong: the left repeat feeds the right line and the other way
        // round, so each echo lands on the opposite side from the last.
        left [(size_t) write] = input + lowR * feedback;
        right[(size_t) write] = lowL * feedback;

        if (++write >= (int) left.size())
            write = 0;

        l += wetL;
        r += wetR;
    }

private:
    float read (const std::vector<float>& line, float delaySamples) const noexcept
    {
        const float size = (float) line.size();
        float position = (float) write - delaySamples;

        while (position < 0.0f)
            position += size;

        const int first = (int) position;
        const int second = (first + 1) % (int) line.size();
        const float frac = position - (float) first;

        return line[(size_t) first] * (1.0f - frac) + line[(size_t) second] * frac;
    }

    double sampleRate = 44100.0;
    std::vector<float> left, right;
    int write = 0;

    float targetDelay = 11025.0f, currentDelay = 11025.0f;
    float sendLevel = 0.0f, feedback = 0.3f;
    float lowL = 0.0f, lowR = 0.0f;
};

} // namespace np::audio
