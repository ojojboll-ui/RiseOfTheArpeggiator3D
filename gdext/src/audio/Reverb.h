#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
    A room for the arpeggio and the chords to hang in.

    Attract Mode's Shimmer would have been the one to borrow, but it wraps
    juce::Reverb, and this project builds without JUCE. So this is the same
    algorithm JUCE's reverb is — Jezar's Freeverb, eight damped combs in
    parallel into four allpasses in series, per side, the right side's delay
    lines a few samples longer so the two decorrelate. Public domain, small,
    and it sounds like the reverb everyone already knows.

    Send-style: `process` reads a mono-summed send and returns only the wet
    signal, added onto the pair.
*/
namespace np::audio
{
    class Reverb
    {
    public:
        void prepare (double sr)
        {
            const double scale = sr / 44100.0;
            static constexpr int combTune[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static constexpr int allTune[4]  { 556, 441, 341, 225 };
            static constexpr int spread = 23;

            for (int i = 0; i < 8; ++i)
            {
                combL[(size_t) i].init ((int) (combTune[i] * scale));
                combR[(size_t) i].init ((int) ((combTune[i] + spread) * scale));
            }

            for (int i = 0; i < 4; ++i)
            {
                allL[(size_t) i].init ((int) (allTune[i] * scale));
                allR[(size_t) i].init ((int) ((allTune[i] + spread) * scale));
            }

            setRoom (room, damping);
        }

        /** size 0..1 (how long), damp 0..1 (how dark the tail gets). */
        void setRoom (float size, float damp) noexcept
        {
            room = std::clamp (size, 0.0f, 1.0f);
            damping = std::clamp (damp, 0.0f, 1.0f);

            const float feedback = 0.7f + 0.28f * room;
            for (auto* c : { &combL, &combR })
                for (auto& comb : *c) { comb.feedback = feedback; comb.damp = damping * 0.4f; }
        }

        /** `sendL/R` is what goes in; the wet result is added to outL/outR. */
        void process (float sendL, float sendR, float& outL, float& outR) noexcept
        {
            const float in = (sendL + sendR) * kInputGain;
            float l = 0.0f, r = 0.0f;

            for (int i = 0; i < 8; ++i)
            {
                l += combL[(size_t) i].process (in);
                r += combR[(size_t) i].process (in);
            }

            for (int i = 0; i < 4; ++i)
            {
                l = allL[(size_t) i].process (l);
                r = allR[(size_t) i].process (r);
            }

            outL += l;
            outR += r;
        }

    private:
        static constexpr float kInputGain = 0.015f;

        struct Comb
        {
            std::vector<float> buf;
            int pos = 0;
            float store = 0.0f, feedback = 0.84f, damp = 0.2f;

            void init (int n) { buf.assign ((size_t) std::max (1, n), 0.0f); pos = 0; store = 0.0f; }

            float process (float x) noexcept
            {
                const float out = buf[(size_t) pos];
                store = out * (1.0f - damp) + store * damp;
                buf[(size_t) pos] = x + store * feedback;
                if (++pos >= (int) buf.size()) pos = 0;
                return out;
            }
        };

        struct Allpass
        {
            std::vector<float> buf;
            int pos = 0;

            void init (int n) { buf.assign ((size_t) std::max (1, n), 0.0f); pos = 0; }

            float process (float x) noexcept
            {
                const float b = buf[(size_t) pos];
                buf[(size_t) pos] = x + b * 0.5f;
                if (++pos >= (int) buf.size()) pos = 0;
                return b - x;
            }
        };

        std::array<Comb, 8> combL, combR;
        std::array<Allpass, 4> allL, allR;
        float room = 0.8f, damping = 0.35f;
    };
}
