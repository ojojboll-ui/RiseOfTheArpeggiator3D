#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
    A drum machine for four on the floor.

    Chordtroller's DrumKit was the first place to look and it is the wrong
    shape for this: it was built to sit behind a plate as accompaniment, so its
    kick is a polite sine. A floor that pumps needs a kick with a click on the
    front, a pitch drop that reads as a punch and enough drive that it is felt
    rather than heard. So this is new, in the same spirit — everything made,
    nothing sampled.

    - **Kick:** a sine falling from about 170 Hz above its rest pitch in a few
      tens of milliseconds, a 3 ms noise click, and a tanh drive normalised so
      the drive changes the colour and not the level.
    - **Clap:** three bandpassed noise bursts eleven milliseconds apart and a
      tail — the burst spacing is what makes a clap a clap and not a snare.
    - **Snare:** a tuned body under highpassed noise.
    - **Hats:** six square waves at the 808's inharmonic ratios, highpassed.
      Metal, not hiss; noise hats sound like a radio between stations. The
      closed hat chokes the open one, as on every drum machine.
    - **Shaker, perc, crash** for the upper layers.

    No JUCE, no allocation. One `process` per sample adds into a stereo pair.
*/
namespace np::audio
{
    /** Per-song colour of the kit. Times in seconds, pitches in hertz. */
    struct DrumTuning
    {
        float kickPitch  = 48.0f;    // where the drop lands
        float kickDrop   = 170.0f;   // how far above that it starts
        float kickDecay  = 0.32f;
        float kickDrive  = 1.8f;
        float kickClick  = 0.45f;
        float clapTone   = 1150.0f;
        float snareTone  = 185.0f;
        float hatTone    = 1.0f;     // multiplies the 808 ratios
        float percPitch  = 520.0f;
        float level      = 1.0f;
    };

    /** A zero-delay state-variable filter, solved rather than approximated. */
    struct Svf
    {
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;

        void set (float hz, float q, double sr) noexcept
        {
            const float g = std::tan (3.14159265f * std::min (hz, 0.45f * (float) sr) / (float) sr);
            k  = 1.0f / std::max (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void process (float v0) noexcept
        {
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = v0 - k * v1 - v2;
        }

        void reset() noexcept { ic1 = ic2 = lp = bp = hp = 0.0f; }
    };

    class PumpKit
    {
    public:
        enum Piece { kick, clap, snare, closedHat, openHat, shaker, perc, crash, boom, swoosh };

        void prepare (double sr) noexcept
        {
            sampleRate = sr > 0.0 ? sr : 48000.0;
            setTuning (tuning);
            reset();
        }

        void reset() noexcept
        {
            kickEnv = kickPitchEnv = clickEnv = 0.0f;
            clapAge = -1;
            snareTone = snareHiss = 0.0f;
            closedEnv = openEnv = shakerEnv = percEnv = crashEnv = boomEnv = 0.0f;
            swooshAge = -1;
            for (auto* f : { &clapBp, &snareHp, &hatHp, &hatBp, &shakerHp, &crashHpL, &crashHpR, &boomLp, &swooshL, &swooshR })
                f->reset();
        }

        void setTuning (const DrumTuning& t) noexcept
        {
            tuning = t;

            kickDecayCoef  = decay (t.kickDecay);
            kickPitchCoef  = decay (0.042f);
            clickCoef      = decay (0.003f);
            clapTailCoef   = decay (0.16f);
            snareToneCoef  = decay (0.08f);
            snareHissCoef  = decay (0.17f);
            closedCoef     = decay (0.045f);
            openCoef       = decay (0.32f);
            shakerCoef     = decay (0.035f);
            percCoef       = decay (0.07f);
            crashCoef      = decay (1.7f);
            boomCoef       = decay (1.4f);

            clapBp.set (t.clapTone, 1.6f, sampleRate);
            snareHp.set (1800.0f, 0.7f, sampleRate);
            hatHp.set (7200.0f, 0.8f, sampleRate);
            hatBp.set (10500.0f, 0.9f, sampleRate);
            shakerHp.set (6500.0f, 0.7f, sampleRate);
            crashHpL.set (4200.0f, 0.6f, sampleRate);
            crashHpR.set (4700.0f, 0.6f, sampleRate);

            driveNorm = 1.0f / std::tanh (std::max (0.1f, t.kickDrive));

            static constexpr float ratios[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            for (int i = 0; i < 6; ++i)
                hatInc[(size_t) i] = ratios[i] * t.hatTone / (float) sampleRate;
        }

        void trigger (Piece piece, float velocity) noexcept
        {
            const float v = std::clamp (velocity, 0.0f, 1.5f) * tuning.level;

            switch (piece)
            {
                case kick:
                    kickEnv = v;
                    kickPitchEnv = 1.0f;
                    clickEnv = v * tuning.kickClick;
                    // Restart the phase at zero on every hit: a kick that starts
                    // mid-cycle has a different first transient each time, and the
                    // first transient is the whole punch.
                    kickPhase = 0.0f;
                    break;

                case clap:      clapAge = 0; clapLevel = v; break;
                case snare:     snareTone = v * 0.7f; snareHiss = v; break;
                case closedHat: closedEnv = v; openEnv = 0.0f; break;    // choke
                case openHat:   openEnv = v; break;
                case shaker:    shakerEnv = v; shakerAttack = 0.0f; break;
                case perc:      percEnv = v; percPitchEnv = 1.0f; break;
                case crash:     crashEnv = v; break;
                case boom:
                    boomEnv = v;
                    boomSweep = 1.0f;
                    break;
                case swoosh:
                    swooshAge = 0;
                    swooshLevel = v;
                    break;
            }
        }

        /** The swoosh, the bomb's answer to the button, on its own so it can
            skip the slow-motion filter. Noise through a bandpass falling from
            about 6 kHz to 500 Hz in 0.12 s, swept from left to right, with a
            6 ms burst of plain noise on the front: the ear times a sound by its
            onset, and a thin hiss alone read as late (2026-10-13). Call every
            sample. */
        void processSwoosh (float& l, float& r) noexcept
        {
            if (swooshAge < 0) return;

            const float t = (float) swooshAge / (float) sampleRate;
            const float p = std::min (1.0f, t / 0.12f);
            const float hz = 6000.0f * std::pow (2.0f, -3.6f * p);
            swooshL.set (hz, 1.4f, sampleRate);
            swooshR.set (hz * 1.06f, 1.4f, sampleRate);
            const float nl = noise(), nr = noise();
            swooshL.process (nl);
            swooshR.process (nr);
            const float env = std::min (1.0f, t / 0.002f) * std::exp (-t / 0.06f) * swooshLevel;
            const float burst = std::exp (-t / 0.006f) * 0.5f * swooshLevel;
            l += (swooshL.bp * env * (1.0f - 0.6f * p) + nl * burst) * 0.9f;
            r += (swooshR.bp * env * (0.4f + 0.6f * p) + nr * burst) * 0.9f;
            if (++swooshAge > (int) (sampleRate * 0.45)) swooshAge = -1;
        }

        /** Adds one sample of the kit into l and r. */
        void process (float& l, float& r) noexcept
        {
            float centre = 0.0f, left = 0.0f, right = 0.0f;

            // Kick ------------------------------------------------------------
            if (kickEnv > kFloor || clickEnv > kFloor)
            {
                const float hz = tuning.kickPitch + tuning.kickDrop * kickPitchEnv * kickPitchEnv;
                kickPhase += hz / (float) sampleRate;
                if (kickPhase >= 1.0f) kickPhase -= 1.0f;

                float k = std::sin (kTwoPi * kickPhase) * kickEnv;
                k += (noise() * 0.6f) * clickEnv;
                k = std::tanh (k * tuning.kickDrive) * driveNorm;

                centre += k;

                kickEnv      *= kickDecayCoef;
                kickPitchEnv *= kickPitchCoef;
                clickEnv     *= clickCoef;
            }

            const float n = noise();

            // Clap: three bursts and a tail -------------------------------------
            if (clapAge >= 0)
            {
                const float t = (float) clapAge / (float) sampleRate;
                float e;

                if (t < 0.033f)
                {
                    const float since = std::fmod (t, 0.011f);
                    e = std::exp (-since / 0.0022f);
                }
                else
                {
                    e = 0.75f * std::exp (-(t - 0.033f) / 0.085f);   // tail 0.14 s until 2026-10-08: shorter
                }

                clapBp.process (n);
                centre += clapBp.bp * e * clapLevel * 1.2f;   // 1.9, then 1.5; -2 dB again 2026-10-08

                if (++clapAge > (int) (sampleRate * 1.2)) clapAge = -1;
            }

            // Snare --------------------------------------------------------------
            if (snareHiss > kFloor || snareTone > kFloor)
            {
                snarePhase += tuning.snareTone / (float) sampleRate;
                if (snarePhase >= 1.0f) snarePhase -= 1.0f;

                snareHp.process (n);
                centre += std::sin (kTwoPi * snarePhase) * snareTone * 0.7f
                        + snareHp.hp * snareHiss * 0.55f;

                snareTone *= snareToneCoef;
                snareHiss *= snareHissCoef;
            }

            // Hats share one metal source ---------------------------------------
            if (closedEnv > kFloor || openEnv > kFloor || crashEnv > kFloor)
            {
                float metal = 0.0f;
                for (int i = 0; i < 6; ++i)
                {
                    auto& p = hatPhase[(size_t) i];
                    p += hatInc[(size_t) i];
                    if (p >= 1.0f) p -= 1.0f;
                    metal += p < 0.5f ? 1.0f : -1.0f;
                }
                metal *= 1.0f / 6.0f;

                hatHp.process (metal);
                hatBp.process (hatHp.hp);
                const float hat = hatBp.bp * 1.6f + hatHp.hp * 0.4f;

                // Closed hat louder and less one-sided (was 0.55, right only):
                // the first two hat layers were too quiet, 2026-10-07.
                left  += hat * closedEnv * 0.45f;
                right += hat * closedEnv * 0.80f;
                left  += hat * openEnv * 0.45f;
                right += hat * openEnv * 0.25f;

                closedEnv *= closedCoef;
                openEnv   *= openCoef;

                if (crashEnv > kFloor)
                {
                    const float n2 = noise();
                    crashHpL.process (n * 0.6f + metal * 0.5f);
                    crashHpR.process (n2 * 0.6f + metal * 0.5f);
                    left  += crashHpL.hp * crashEnv * 0.42f;
                    right += crashHpR.hp * crashEnv * 0.42f;
                    crashEnv *= crashCoef;
                }
            }

            // Shaker: noise with a soft front, on the left -----------------------
            if (shakerEnv > kFloor)
            {
                shakerAttack = std::min (1.0f, shakerAttack + 1.0f / (0.004f * (float) sampleRate));
                shakerHp.process (n);
                left += shakerHp.hp * shakerEnv * shakerAttack * 0.4f;
                shakerEnv *= shakerCoef;
            }

            // Perc: a tuned blip that bends down a little, alternating sides -----
            if (percEnv > kFloor)
            {
                percPhase += tuning.percPitch * (1.0f + 0.35f * percPitchEnv) / (float) sampleRate;
                if (percPhase >= 1.0f) percPhase -= 1.0f;
                const float p = std::sin (kTwoPi * percPhase) * percEnv * 0.5f;
                left  += p * 0.6f;
                right += p * 0.4f;
                percEnv *= percCoef;
                percPitchEnv *= kickPitchCoef;
            }

            // Boom: the death. Low noise falling through a closing lowpass ------
            if (boomEnv > kFloor)
            {
                boomSweep *= 0.99993f;
                boomLp.set (60.0f + 2400.0f * boomSweep * boomSweep, 1.2f, sampleRate);
                boomLp.process (n);
                centre += boomLp.lp * boomEnv * 1.4f;
                boomEnv *= boomCoef;
            }

            l += centre + left;
            r += centre + right;
        }

    private:
        static constexpr float kTwoPi = 6.28318530718f;
        static constexpr float kFloor = 1.0e-4f;

        float decay (float seconds) const noexcept
        {
            return std::pow (0.001f, 1.0f / std::max (1.0f, (float) (sampleRate * seconds)));
        }

        float noise() noexcept
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (std::int32_t) rng * (1.0f / 2147483648.0f);
        }

        double sampleRate = 48000.0;
        DrumTuning tuning;

        float kickEnv = 0.0f, kickPitchEnv = 0.0f, clickEnv = 0.0f, kickPhase = 0.0f, driveNorm = 1.0f;
        float kickDecayCoef = 0.999f, kickPitchCoef = 0.99f, clickCoef = 0.9f;

        int   clapAge = -1;
        float clapLevel = 0.0f, clapTailCoef = 0.999f;

        float snareTone = 0.0f, snareHiss = 0.0f, snarePhase = 0.0f;
        float snareToneCoef = 0.999f, snareHissCoef = 0.999f;

        std::array<float, 6> hatPhase {}, hatInc {};
        float closedEnv = 0.0f, openEnv = 0.0f, closedCoef = 0.99f, openCoef = 0.999f;

        float shakerEnv = 0.0f, shakerAttack = 0.0f, shakerCoef = 0.99f;
        float percEnv = 0.0f, percPitchEnv = 0.0f, percPhase = 0.0f, percCoef = 0.99f;
        float crashEnv = 0.0f, crashCoef = 0.9999f;
        float boomEnv = 0.0f, boomSweep = 0.0f, boomCoef = 0.9999f;
        int swooshAge = -1;
        float swooshLevel = 0.0f;

        Svf clapBp, snareHp, hatHp, hatBp, shakerHp, crashHpL, crashHpR, boomLp, swooshL, swooshR;

        std::uint32_t rng = 0x6d2b79f5u;
    };
}
