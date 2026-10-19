#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Ensemble.h"
#include "JunoVoice.h"

/**
    The chord an enemy lets out when it is hit.

    Adapted from Attract Mode's JunoChords (breakout/Source/audio/JunoChords.h,
    the asteroid chord) on 2026-10-07. The engine is the same — Juno voices
    through a Juno chorus per side, one patch per size, the bigger the darker
    and the longer — with two changes:

    - **A bank per song.** The asteroid's three patches are bank 0, untouched.
      The acid song gets short brassy stabs and the trance song a slow pad
      with a long tail, because one timbre across three songs would make the
      three songs one song.
    - **Sixteen voices instead of twelve.** Here a chord is four or five notes
      and a 1/16 can carry two chords at once — and it often does, because
      Geometry Wars kills in handfuls.

    Sizes: 0 small and bright, 1 middling, 2 large, dark and long.
*/
namespace np::audio
{
    struct HitBank
    {
        std::array<JunoPatch, 3> patches;
        std::array<float, 3> cutoff;     // where each size settles on the slider
        std::array<float, 3> gate;       // seconds held before release
        float resonance;
    };

    inline const HitBank& hitBank (int index) noexcept
    {
        static const std::array<HitBank, 3> banks
        {{
            // 0: the asteroid chord's patches. Since 2026-10-07 every bank holds
            //    its gate about 1.6 times longer and settles half an octave
            //    brighter, so bursts carry in the middle of the mix ("höras mer").
            {{{
                // name      saw   pul   sub   noise  pw    pwm   lfo    env   key   hpf    A      D     S     R     lvl   dly  fb   div
                { "SMA",     0.75f,0.45f,0.30f,0.00f, 0.45f,0.35f,0.45f, 0.34f,0.30f,0.05f, 0.003f,0.60f,0.25f,0.90f, 0.62f, 0.0f,0.0f,0 },
                { "MELLAN",  0.75f,0.35f,0.55f,0.00f, 0.50f,0.40f,0.45f, 0.30f,0.30f,0.03f, 0.006f,1.60f,0.50f,2.40f, 0.68f, 0.0f,0.0f,0 },
                { "STORA",   0.70f,0.25f,0.90f,0.03f, 0.50f,0.45f,0.45f, 0.28f,0.25f,0.01f, 0.010f,3.00f,0.62f,4.50f, 0.76f, 0.0f,0.0f,0 }
             }},
             { 0.44f, 0.35f, 0.26f }, { 0.40f, 0.95f, 1.90f }, 0.266f },

            // 1: stabs. Chordtroller's STOT and BRASS pulled shorter: a stab that
            //    rings is a pad, and a 303 underneath wants room.
            {{{
                { "STOT",    0.90f,0.25f,0.40f,0.00f, 0.50f,0.10f,0.80f, 0.60f,0.40f,0.05f, 0.002f,0.22f,0.20f,0.20f, 0.72f, 0.0f,0.0f,0 },
                { "STOT2",   0.95f,0.20f,0.45f,0.00f, 0.50f,0.10f,0.80f, 0.55f,0.40f,0.04f, 0.003f,0.40f,0.30f,0.45f, 0.74f, 0.0f,0.0f,0 },
                { "BRASS",   1.00f,0.00f,0.50f,0.02f, 0.50f,0.00f,0.50f, 0.55f,0.35f,0.03f, 0.020f,0.90f,0.55f,1.60f, 0.78f, 0.0f,0.0f,0 }
             }},
             { 0.46f, 0.39f, 0.31f }, { 0.14f, 0.32f, 1.10f }, 0.38f },

            // 2: a pad that blooms and hangs on, for the trance song.
            {{{
                { "GLAS",    0.60f,0.60f,0.20f,0.00f, 0.40f,0.40f,0.50f, 0.36f,0.30f,0.10f, 0.004f,0.90f,0.35f,1.60f, 0.60f, 0.0f,0.0f,0 },
                { "PAD",     0.80f,0.45f,0.30f,0.00f, 0.50f,0.35f,0.45f, 0.32f,0.30f,0.05f, 0.010f,1.80f,0.60f,3.20f, 0.66f, 0.0f,0.0f,0 },
                { "VALV",    0.80f,0.30f,0.80f,0.04f, 0.50f,0.45f,0.40f, 0.30f,0.25f,0.02f, 0.030f,3.50f,0.70f,6.00f, 0.74f, 0.0f,0.0f,0 }
             }},
             { 0.48f, 0.39f, 0.29f }, { 0.50f, 1.25f, 2.40f }, 0.30f }
        }};

        return banks[(size_t) std::clamp (index, 0, 2)];
    }

    class HitChords
    {
    public:
        static constexpr int kVoices = 16;

        void prepare (double sr)
        {
            sampleRate = sr > 0.0 ? sr : 48000.0;

            for (auto& v : voices) v.prepare (sampleRate);

            for (auto& e : chorus)
            {
                e.prepare (sampleRate);
                e.setRate (kChorusRate);
                e.setMix (kChorusMix);
            }

            lfoPhase = 0.0f;
            order = 0;
        }

        void setBank (int index) noexcept { bank = &hitBank (index); }

        void allOff() noexcept { for (auto& v : voices) v.noteOff(); }

        /** `gateSeconds` overrides the bank's hold time when positive. */
        void trigger (int note, float velocity, int size, float panPosition, float gateSeconds = -1.0f) noexcept
        {
            const auto s = std::clamp (size, 0, 2);

            int slot = -1;
            std::uint64_t oldest = ~(std::uint64_t) 0;

            for (int i = 0; i < kVoices; ++i)
            {
                const auto& v = voices[(size_t) i];
                if (! v.isActive()) { slot = i; break; }
                if (v.age() < oldest) { oldest = v.age(); slot = i; }
            }

            auto& v = voices[(size_t) slot];

            v.setPatch (bank->patches[(size_t) s]);
            v.noteOn (note, std::clamp (velocity, 0.0f, 1.0f),
                      (int) (sampleRate * (gateSeconds > 0.0f ? gateSeconds : bank->gate[(size_t) s])), ++order);

            voiceSize[(size_t) slot] = s;

            const auto p = std::clamp (panPosition, 0.0f, 1.0f) * 1.5707963f;
            gainL[(size_t) slot] = std::cos (p);
            gainR[(size_t) slot] = std::sin (p);

        }

        /** Adds into the buffers so the caller can render in segments. */
        void render (float* left, float* right, int numSamples) noexcept
        {
            // No idle shortcut, unlike JunoChords: skipping the chorus while
            // silent moves its LFO by whole blocks, and then the next chord
            // sounds different depending on the buffer size.
            const auto active = anyActive();

            const auto lfoStep = kLfoRate / (float) sampleRate;

            for (int n = 0; n < numSamples; ++n)
            {
                lfoPhase += lfoStep;
                if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

                const auto lfo = std::sin (6.2831853f * lfoPhase);

                float l = 0.0f, r = 0.0f;

                if (active)
                {
                    for (int i = 0; i < kVoices; ++i)
                    {
                        auto& v = voices[(size_t) i];
                        if (! v.isActive()) continue;

                        const auto x = v.process (lfo, bank->cutoff[(size_t) voiceSize[(size_t) i]],
                                                  bank->resonance, 1.0f) * kVoiceGain;
                        l += x * gainL[(size_t) i];
                        r += x * gainR[(size_t) i];
                    }
                }

                float cl = 0.0f, cr = 0.0f, unused = 0.0f;
                chorus[0].process (l, cl, unused);
                chorus[1].process (r, unused, cr);

                left[n]  += cl;
                right[n] += cr;
            }
        }

        bool anyActive() const noexcept
        {
            for (const auto& v : voices) if (v.isActive()) return true;
            return false;
        }

        int activeVoices() const noexcept
        {
            int n = 0;
            for (const auto& v : voices) if (v.isActive()) ++n;
            return n;
        }

    private:
        static constexpr float kLfoRate    = 0.45f;
        static constexpr float kVoiceGain  = 0.42f;
        static constexpr float kChorusRate = 0.5f;
        static constexpr float kChorusMix  = 0.5f;

        std::array<JunoVoice, kVoices> voices {};
        std::array<int,   kVoices> voiceSize {};
        std::array<float, kVoices> gainL {};
        std::array<float, kVoices> gainR {};
        std::array<Ensemble, 2> chorus {};

        const HitBank* bank = &hitBank (0);
        double sampleRate = 48000.0;
        float lfoPhase = 0.0f;
        std::uint64_t order = 0;
    };
}
