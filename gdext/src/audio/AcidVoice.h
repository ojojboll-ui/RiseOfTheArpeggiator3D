#pragma once

// Neonpuls: copied from breakout/Source/audio/AcidVoice.h on 2026-10-07. Only the namespace
// changed (now np::audio); if it is fixed in one place, fix it in the other.

#include <algorithm>
#include <cmath>
#include <cstdint>

/*
    Borrowed from Chordtroller (chordtroller/Source/dsp/AcidVoice.h) on
    2026-09-16, for the shots in the asteroids cabinet. Only the voice came
    across, not the pattern sequencer around it. Unchanged apart from the
    namespace; if it is fixed in one place, fix it in the other.
*/
namespace np::audio
{

/**
    One voice shaped like a TB-303's.

    What makes that machine sound like itself is not the oscillator, which is a
    plain saw or square, and not really the filter either. It is how four things
    are wired together:

    - **Two envelopes, not one.** Loudness follows the gate. The filter envelope
      ignores the gate and only decays — DECAY says how long — so a long note
      keeps its level while the brightness drains out of it.
    - **Accent** is a louder note whose filter sweep is forced short and pushed
      through a small capacitor. The capacitor is the famous part: it smears the
      sweep into a "wow", and when accents come close together its charge has
      not drained, so a run of them climbs.
    - **Slide** keeps the gate open into the next note and moves the pitch with
      an RC glide instead of a jump. The envelopes do not restart, which is what
      makes a slid phrase one gesture rather than a string of notes.
    - **Resonance thins the bottom.** Turned up, the level drops rather than
      booms, so there is less make-up gain here than on the Juno.

    The filter is the same zero-delay four-pole ladder as the Juno's, with less
    feedback headroom. It is not a model of the 303's diode ladder and does not
    pretend to be — it is a ladder voiced towards one.
*/
class AcidVoice
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;

        attackStep    = 1.0f / std::max (1.0f, (float) (sampleRate * 0.003));
        holdCoef      = coefficientFor (1.8f);
        releaseCoef   = coefficientFor (0.012f);
        accentMegCoef = coefficientFor (0.30f);
        accentFall    = coefficientFor (1.0f);
        accentCharge  = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.018));
        dcCoef        = 1.0f - std::exp (-2.0f * kPi * 20.0f / (float) sampleRate);

        setGlideSeconds (glideSeconds);
        setDecay (decayKnob);
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0f;
        pitch = target = 48.0f;
        veg = meg = accentEnv = accentSmooth = 0.0f;
        s1 = s2 = s3 = s4 = 0.0f;
        dcState = 0.0f;
        stage = Stage::idle;
        gate = accented = false;
    }

    void setSquare (bool on) noexcept { square = on; }

    /** The glide's RC time constant. */
    void setGlideSeconds (float seconds) noexcept
    {
        glideSeconds = std::max (0.001f, seconds);
        glideCoef = 1.0f - std::exp (-1.0f / std::max (1.0f, (float) (sampleRate * glideSeconds)));
    }

    /** Knob positions for this block, 0..1, resonance already tapered. */
    void setFilter (float cutoffPosition, float resonancePosition) noexcept
    {
        cutoff    = std::clamp (cutoffPosition, 0.0f, 1.0f);
        resonance = std::clamp (resonancePosition, 0.0f, 1.0f);
    }

    void setEnvMod (float amount) noexcept { envMod = std::clamp (amount, 0.0f, 1.0f); }

    /** A quarter of a second to nearly three, the span of the original's knob. */
    void setDecay (float knob) noexcept
    {
        decayKnob = std::clamp (knob, 0.0f, 1.0f);
        megCoef   = coefficientFor (0.25f * std::pow (2.0f, decayKnob * 3.5f));
    }

    /** A new note: both envelopes start again and the pitch jumps. */
    void trigger (int midiNote, bool accent) noexcept
    {
        pitch = target = (float) midiNote;
        accented = accent;
        gate  = true;
        stage = Stage::attack;

        meg     = 1.0f;
        megFall = accent ? accentMegCoef : megCoef;

        // Charged on top of whatever the last accent left behind. Close accents
        // stack; far apart ones each start from nothing.
        if (accent)
            accentEnv = std::min (1.6f, accentEnv * 0.6f + 1.0f);
    }

    /** Legato: the gate stays open, the envelopes carry on, only the pitch moves. */
    void glideTo (int midiNote, bool accent) noexcept
    {
        target   = (float) midiNote;
        accented = accent;
        gate     = true;

        if (stage == Stage::release || stage == Stage::idle)
            stage = Stage::hold;
    }

    void release() noexcept
    {
        if (! gate)
            return;

        gate  = false;
        stage = Stage::release;
    }

    bool isGateOpen() const noexcept { return gate; }
    bool isSounding() const noexcept { return stage != Stage::idle; }

    float process() noexcept
    {
        // The capacitor drains whether or not anything is playing, so the next
        // accent sees the charge that is really left in it.
        accentEnv    *= accentFall;
        accentSmooth += (accentEnv - accentSmooth) * accentCharge;

        if (stage == Stage::idle)
            return 0.0f;

        meg *= megFall;

        switch (stage)
        {
            case Stage::attack:
                veg += attackStep;
                if (veg >= 1.0f) { veg = 1.0f; stage = Stage::hold; }
                break;

            case Stage::hold:
                veg *= holdCoef;
                break;

            case Stage::release:
                veg *= releaseCoef;
                if (veg < 1.0e-4f) { veg = 0.0f; stage = Stage::idle; }
                break;

            case Stage::idle:
                break;
        }

        pitch += (target - pitch) * glideCoef;

        const float dt = std::min (0.45f, (float) (440.0 * std::pow (2.0, (pitch - 69.0f) / 12.0)
                                                    / sampleRate));

        const float x = square ? (phase < 0.5f ? 1.0f : -1.0f) + blep (phase, dt)
                                   - blep (wrap (phase + 0.5f), dt)
                               : 2.0f * phase - 1.0f - blep (phase, dt);

        phase = wrap (phase + dt);

        // In octaves above 50 Hz: eight on the cutoff, up to six more from the
        // envelope, and the accent's capacitor on top.
        const float octaves = 8.0f * cutoff + 6.0f * envMod * meg + 2.4f * accentSmooth;
        const float hz = std::clamp (50.0f * std::pow (2.0f, octaves), 20.0f,
                                     0.45f * (float) sampleRate);

        const float g = std::tan (kPi * hz / (float) sampleRate);
        const float G = g / (1.0f + g);
        const float k = resonance * 3.7f;

        const float S = (G * G * G * s1 + G * G * s2 + G * s3 + s4) / (1.0f + g);
        const float u = softClip ((x - k * S) / (1.0f + k * G * G * G * G));

        float v = (u  - s1) * G; const float y1 = v + s1; s1 = y1 + v;
        v = (y1 - s2) * G;       const float y2 = v + s2; s2 = y2 + v;
        v = (y2 - s3) * G;       const float y3 = v + s3; s3 = y3 + v;
        v = (y3 - s4) * G;       const float y4 = v + s4; s4 = y4 + v;

        float out = y4 * (1.0f + 0.3f * k) * veg * (accented ? 1.0f : 0.7f);

        dcState += (out - dcState) * dcCoef;
        out -= dcState;

        return softClip (out * 1.2f) * 0.55f;
    }

private:
    enum class Stage { idle, attack, hold, release };

    static constexpr float kPi = 3.14159265358979f;

    float coefficientFor (float seconds) const noexcept
    {
        return std::pow (0.001f, 1.0f / std::max (1.0f, (float) (sampleRate * seconds)));
    }

    static float wrap (float p) noexcept { return p >= 1.0f ? p - 1.0f : p; }

    static float blep (float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    /** Rational tanh: cheap, smooth, and exact enough where it matters. */
    static float softClip (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    double sampleRate = 44100.0;

    float phase = 0.0f, pitch = 48.0f, target = 48.0f;
    float glideSeconds = 0.06f, glideCoef = 0.01f;

    float cutoff = 0.0f, resonance = 0.0f, envMod = 0.5f, decayKnob = 0.4f;

    float veg = 0.0f, attackStep = 0.01f, holdCoef = 0.9999f, releaseCoef = 0.99f;
    float meg = 0.0f, megCoef = 0.999f, megFall = 0.999f, accentMegCoef = 0.999f;
    float accentEnv = 0.0f, accentSmooth = 0.0f, accentFall = 0.9999f, accentCharge = 0.01f;

    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    float dcState = 0.0f, dcCoef = 0.001f;

    Stage stage = Stage::idle;
    bool  gate = false, accented = false, square = false;
};

} // namespace np::audio
