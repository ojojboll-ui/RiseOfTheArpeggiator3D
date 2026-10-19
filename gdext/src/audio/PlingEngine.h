#pragma once

// Neonpuls: copied from breakout/Source/audio/PlingEngine.h on 2026-10-07. Only the
// namespace changed (now np::audio); if it is fixed in one place, fix it in the other.

#include <array>
#include <cmath>

#include "PlingVoice.h"

/**
    The voice pool.

    Thirty-two voices for sixteen balls, because a ball that bounces twice in
    quick succession has to be allowed to ring through its own next hit — a
    voice that cuts itself off is the difference between a struck bar and a
    stutter. When they do run out the quietest one goes, not the oldest: the
    quietest is the one nobody will miss.

    Nothing here allocates and nothing here locks. Fixed array, fixed work.
*/
namespace np::audio
{
    class PlingEngine
    {
    public:
        static constexpr int kVoices = 32;

        void prepare (double sr) noexcept
        {
            sampleRate = sr > 0.0 ? sr : 48000.0;
            for (auto& v : voices) v = PlingVoice {};
        }

        void trigger (int note, float velocity, float tone, float brightness,
                      float decayScale, float pan) noexcept
        {
            const auto freq = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);

            auto* target = &voices[0];
            float quietest = 1.0e30f;

            for (auto& v : voices)
            {
                if (! v.isActive()) { target = &v; quietest = -1.0f; break; }

                if (v.loudness() < quietest) { quietest = v.loudness(); target = &v; }
            }

            target->start (freq, velocity, tone, brightness, decayScale, pan, sampleRate);
        }

        /** Adds into the buffers rather than replacing, so the caller can render
            in segments around sample-accurate triggers without clearing between
            them. */
        void render (float* left, float* right, int numSamples) noexcept
        {
            for (size_t k = 0; k < voices.size(); ++k)
            {
                auto& v = voices[k];
                if (! v.isActive()) continue;

                const auto gl = v.left();
                const auto gr = v.right();

                // Neonpuls: stop on the sample the voice goes quiet. Rendering on
                // to the end of the block made the tail depend on the block size.
                for (int i = 0; i < numSamples && v.isActive(); ++i)
                {
                    const auto s = v.render (rngs[k]);
                    left[i]  += s * gl;
                    right[i] += s * gr;
                }
            }
        }

        int activeVoices() const noexcept
        {
            int n = 0;
            for (const auto& v : voices) if (v.isActive()) ++n;
            return n;
        }

    private:
        std::array<PlingVoice, kVoices> voices {};
        // Neonpuls: one noise generator per voice. A shared one was drawn from
        // voice by voice, block by block, so two voices sounding together got
        // different noise at different buffer sizes.
        std::array<std::uint32_t, kVoices> rngs = makeSeeds();

        static std::array<std::uint32_t, kVoices> makeSeeds() noexcept
        {
            std::array<std::uint32_t, kVoices> s {};
            for (int i = 0; i < kVoices; ++i) s[(size_t) i] = 0x1234567u + 0x9e3779b9u * (std::uint32_t) i;
            return s;
        }
        double sampleRate = 48000.0;
    };
}
