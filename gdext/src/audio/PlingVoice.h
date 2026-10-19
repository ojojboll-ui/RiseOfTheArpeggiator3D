#pragma once

// Neonpuls: copied from breakout/Source/audio/PlingVoice.h on 2026-10-07. Only the namespace
// changed (now np::audio); if it is fixed in one place, fix it in the other.

#include <cmath>
#include <cstdint>

/**
    One pling, somewhere between a music box and a vibraphone.

    Both ends of the knob are struck metal, which is why they can share four
    damped sines and a noise attack — what separates them is which partials are
    there and how long each one lasts.

    A music box tooth is a free steel bar: partials near 1 : 2.76 : 5.4 : 8.9,
    plenty of energy up top, gone in about a second. A vibraphone bar is
    undercut until its modes sit at 1 : 4 : 10, and the undercutting is exactly
    what kills the upper ones fast — so it decays to a nearly pure fundamental
    that rings for seconds, with the motor's tremolo over it. Morphing between
    those two is a real knob, not a crossfade between two samples.

    Modal rather than filtered because the plan's brick colours are going to be
    points in this space. It is also the only kind of voice that survives being
    retriggered thirty times a second: a fast attack and a decaying tail stack
    into a texture, where anything with a sustain stacks into mud.

    Each partial is a rotating unit vector rather than a call to sin(). Four
    multiplies and two adds per partial per sample, no table, no phase to wrap.
*/
namespace np::audio
{
    class PlingVoice
    {
    public:
        static constexpr int kPartials = 4;

        void start (float freq, float velocity, float tone, float brightness,
                    float decayScale, float pan, double sampleRate) noexcept
        {
            static constexpr float boxRatio[kPartials]  { 1.0f, 2.756f, 5.404f, 8.933f };
            static constexpr float vibeRatio[kPartials] { 1.0f, 4.000f, 10.00f, 13.92f };
            static constexpr float boxWeight[kPartials]  { 1.0f, 0.55f, 0.34f, 0.20f };
            static constexpr float vibeWeight[kPartials] { 1.0f, 0.26f, 0.08f, 0.03f };

            const auto sr = (float) sampleRate;
            const auto nyquist = sr * 0.45f;

            // An unshaped bar lets go in about a second. A vibraphone does not.
            const auto decaySeconds = (0.75f + tone * 2.75f) * decayScale;

            // How much faster the upper modes die than the fundamental. This is
            // most of what separates the two ends. The undercutting that tunes
            // a vibraphone bar to 1 : 4 : 10 is also what kills its upper modes
            // within a few tenths of a second, leaving a nearly pure sine
            // ringing for seconds — so a struck vibraphone is bright for an
            // instant and then round. A bar that has not been undercut stays
            // bright the whole way down, and that clangy sustain is the sound
            // people mean by xylophone.
            const auto upperFall = 0.85f + tone * 2.40f;

            level = 0.0f;

            for (int n = 0; n < kPartials; ++n)
            {
                auto& m = modes[n];

                const auto ratio  = boxRatio[n]  + (vibeRatio[n]  - boxRatio[n])  * tone;
                const auto weight = boxWeight[n] + (vibeWeight[n] - boxWeight[n]) * tone;
                const auto f = freq * ratio;

                if (f >= nyquist)
                {
                    m.re = m.im = 0.0f;
                    m.c = 1.0f; m.s = 0.0f; m.decay = 0.0f;
                    continue;
                }

                const auto w = 6.283185307f * f / sr;
                m.c = std::cos (w);
                m.s = std::sin (w);

                const auto t60 = decaySeconds / (1.0f + upperFall * (float) n);
                m.decay = std::exp (-6.9077553f / (t60 * sr));

                const auto amp = weight * velocity * (n == 0 ? 1.0f : brightness);
                m.re = amp;
                m.im = 0.0f;

                level += amp;
            }

            // The contact sound. A music box has a distinct tick as the pin
            // leaves the tooth; a mallet on a vibraphone bar barely has one.
            noise = (0.24f - tone * 0.19f) * velocity;
            noiseDecay = std::exp (-6.9077553f / (0.0025f * sr));

            // The motor. Only at the vibraphone end, where it belongs.
            tremoloDepth = tone * 0.34f;
            const auto tw = 6.283185307f * 5.4f / sr;
            tremC = std::cos (tw);
            tremS = std::sin (tw);
            tremRe = 1.0f;
            tremIm = 0.0f;

            const auto p = pan < 0.0f ? 0.0f : (pan > 1.0f ? 1.0f : pan);
            gainL = std::cos (p * 1.5707963f);
            gainR = std::sin (p * 1.5707963f);

            envDecay = modes[0].decay;
            active = level > 1.0e-5f;
        }

        bool isActive() const noexcept { return active; }
        float loudness() const noexcept { return level; }

        float render (std::uint32_t& rng) noexcept
        {
            float out = 0.0f;

            for (auto& m : modes)
            {
                const auto re = m.re * m.c - m.im * m.s;
                const auto im = m.re * m.s + m.im * m.c;

                m.re = re * m.decay;
                m.im = im * m.decay;

                out += m.im;
            }

            if (tremoloDepth > 1.0e-4f)
            {
                const auto re = tremRe * tremC - tremIm * tremS;
                const auto im = tremRe * tremS + tremIm * tremC;
                tremRe = re;
                tremIm = im;

                out *= 1.0f - tremoloDepth * (0.5f - 0.5f * tremRe);
            }

            if (noise > 1.0e-6f)
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                out += noise * ((float) (rng & 0xFFFF) * (1.0f / 32768.0f) - 1.0f);
                noise *= noiseDecay;
            }

            level *= envDecay;

            if (level < 1.0e-5f) active = false;

            return out * 0.42f;
        }

        float left() const noexcept  { return gainL; }
        float right() const noexcept { return gainR; }

    private:
        struct Mode { float re = 0.0f, im = 0.0f, c = 1.0f, s = 0.0f, decay = 0.0f; };

        Mode modes[kPartials];
        float noise = 0.0f, noiseDecay = 0.0f;
        float level = 0.0f, envDecay = 0.0f;
        float tremoloDepth = 0.0f, tremRe = 1.0f, tremIm = 0.0f, tremC = 1.0f, tremS = 0.0f;
        float gainL = 0.707f, gainR = 0.707f;
        bool active = false;
    };
}
