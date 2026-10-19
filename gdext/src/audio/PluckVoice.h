#pragma once

// Neonpuls: copied from chordtroller/Source/dsp/PluckVoice.h on 2026-10-07. Only the namespace
// changed (now np::audio); if it is fixed in one place, fix it in the other.

#include "VoiceSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace np::audio
{

/**
    One string, plucked.

    Two detuned oscillators, a fast attack, an exponential decay, and a lowpass
    that closes as the note dies — that last part is most of what makes a pluck
    sound plucked rather than switched off. Which oscillator, how open the
    filter and how long the ring all come from the VoiceSpec, so this class never
    learns which timbre it is.

    Two details are here because leaving them out makes noises that get mistaken
    for character: pulse and saw are band-limited, because the top octave is over
    2 kHz and a naive edge folds its harmonics back down as a metallic buzz; and
    re-plucking a ringing string keeps the oscillator phase, because restarting
    it would click.
*/
class PluckVoice
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;
        reset();
    }

    void reset() noexcept
    {
        active = attacking = false;
        env = 0.0f;
        lowpass = 0.0f;
        phase1 = 0.0f;
        phase2 = 0.37f;
        stringIdx = -1;
        stamp = 0;
    }

    void start (float frequencyHz, float velocity, float decayCoefficient,
                const VoiceSpec& voice, int stringIndex, std::uint64_t timeStamp) noexcept
    {
        const bool wasActive = active;

        spec = &voice;

        const float spread = std::pow (2.0f, voice.detuneCents / 1200.0f);
        inc1 = (float) (frequencyHz / spread / sampleRate);
        inc2 = (float) (frequencyHz * spread / sampleRate);

        // Only a silent voice starts its oscillators from the top. A string
        // being struck again is still moving, and resetting its phase is exactly
        // the discontinuity that clicks.
        if (! wasActive)
        {
            phase1  = 0.0f;
            phase2  = 0.37f;    // out of step, so the pair does not cancel at onset
            lowpass = 0.0f;
        }

        target     = std::clamp (velocity, 0.0f, 1.0f);
        attackStep = target / std::max (1.0f, (float) (sampleRate * (double) voice.attackSeconds));
        attacking  = true;
        decayCoef  = decayCoefficient;

        const float cutoff = std::clamp (frequencyHz * voice.filterMultiple, 800.0f, 14000.0f);
        filterG = 1.0f - std::exp (-2.0f * kPi * cutoff / (float) sampleRate);

        stringIdx = stringIndex;
        stamp     = timeStamp;
        active    = true;
    }

    /** The trigger lands here, retargeting notes that are already ringing rather
        than only the next one. Easing it has to shorten what is sounding now, or
        it is not a sustain control. */
    void setDecayCoefficient (float c) noexcept { decayCoef = c; }

    /** Instant Off. Not actually instant — a hard stop is a click. Five
        milliseconds reads as immediate and does not. */
    void stopQuickly() noexcept
    {
        if (! active)
            return;

        attacking = false;
        decayCoef = std::pow (0.001f, 1.0f / std::max (1.0f, (float) (sampleRate * 0.005)));
    }

    bool          isActive() const noexcept { return active; }
    int           string()   const noexcept { return stringIdx; }
    std::uint64_t age()      const noexcept { return stamp; }

    float process() noexcept
    {
        if (! active || spec == nullptr)
            return 0.0f;

        if (attacking)
        {
            env += attackStep;

            if (env >= target)
            {
                env = target;
                attacking = false;
            }
        }
        else
        {
            env *= decayCoef;

            if (env < 1.0e-4f)
            {
                active = false;
                env = 0.0f;
                return 0.0f;
            }
        }

        const float raw = (oscillator (phase1, inc1) + oscillator (phase2, inc2)) * 0.5f;

        phase1 += inc1; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += inc2; if (phase2 >= 1.0f) phase2 -= 1.0f;

        const float g = std::clamp (filterG * (0.35f + 0.65f * env), 0.0005f, 0.99f);
        lowpass += g * (raw - lowpass);

        return lowpass * env;
    }

private:
    static constexpr float kPi = 3.14159265358979f;

    /** The correction that turns a stepped edge into a band-limited one. */
    static float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f)
            return 0.0f;

        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }

        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }

    float oscillator (float phase, float inc) const noexcept
    {
        switch (spec->waveform)
        {
            case Waveform::sine:
                return std::sin (2.0f * kPi * phase);

            case Waveform::saw:
                return (2.0f * phase - 1.0f) - polyBlep (phase, inc);

            case Waveform::pulse:
            default:
            {
                const float width = spec->pulseWidth;

                float v = phase < width ? 1.0f : -1.0f;
                v += polyBlep (phase, inc);

                float falling = phase + (1.0f - width);
                if (falling >= 1.0f)
                    falling -= 1.0f;

                return v - polyBlep (falling, inc);
            }
        }
    }

    const VoiceSpec* spec = nullptr;

    double sampleRate = 44100.0;

    float phase1 = 0.0f, phase2 = 0.37f;
    float inc1 = 0.0f, inc2 = 0.0f;

    float env = 0.0f, target = 1.0f, attackStep = 0.01f, decayCoef = 0.999f;
    float lowpass = 0.0f, filterG = 0.5f;

    bool active = false, attacking = false;

    int           stringIdx = -1;
    std::uint64_t stamp     = 0;
};

} // namespace np::audio
