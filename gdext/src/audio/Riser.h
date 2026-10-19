#pragma once

#include <cmath>
#include <cstdint>

#include "Drums.h"

/**
    The noise riser before a big layer comes in (asked for 2026-10-08, "en cool
    riser (noise filtersvep) innan paden kommer in").

    White noise through a bandpass per side, the two sides fed from different
    noise so it is wide. Over its length the band climbs six octaves, from
    about 220 Hz to 14 kHz, in equal octaves per beat so the ear hears a steady
    climb; the resonance tightens and the level swells, so it ends as a bright
    hiss on the downbeat where the crash takes over. It stops dead there,
    which is what makes the downbeat land.

    Started and stopped on ticks, run per sample: the block size cannot move it.
*/
namespace np::audio
{
    class Riser
    {
    public:
        void prepare (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 48000.0; stop(); }

        void start (std::int64_t lengthSamples) noexcept
        {
            length = lengthSamples > 0 ? lengthSamples : 1;
            age = 0;
            active = true;
            l.reset(); r.reset();
        }

        void stop() noexcept { active = false; }
        bool isActive() const noexcept { return active; }

        /** How far through the sweep, 0..1. */
        float progress() const noexcept { return active ? (float) age / (float) length : 1.0f; }

        void process (float& outL, float& outR) noexcept
        {
            if (! active) return;

            const float p = (float) age / (float) length;
            if (p >= 1.0f) { active = false; return; }

            const float hz = 220.0f * std::pow (2.0f, 6.0f * p);
            const float q = 1.2f + 3.5f * p;
            l.set (hz, q, sampleRate);
            r.set (hz * 1.03f, q, sampleRate);

            l.process (noise (rngL));
            r.process (noise (rngR));

            // Level in dB terms: from -30 dB to 0 over the length, so it swells
            // evenly to the ear rather than arriving all at the end.
            const float level = std::pow (10.0f, (-30.0f + 30.0f * p) / 20.0f);

            outL += l.bp * level * kGain;
            outR += r.bp * level * kGain;
            ++age;
        }

    private:
        static constexpr float kGain = 0.6f;   // 0.9 until 2026-10-09: -3.5 dB

        static float noise (std::uint32_t& s) noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (std::int32_t) s * (1.0f / 2147483648.0f);
        }

        Svf l, r;
        double sampleRate = 48000.0;
        std::int64_t length = 1, age = 0;
        bool active = false;
        std::uint32_t rngL = 0x1b873593u, rngR = 0xcc9e2d51u;
    };
}
