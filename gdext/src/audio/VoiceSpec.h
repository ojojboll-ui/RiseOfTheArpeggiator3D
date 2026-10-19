#pragma once

// Neonpuls: copied from chordtroller/Source/dsp/VoiceSpec.h on 2026-10-07. Only the namespace
// changed (now np::audio); if it is fixed in one place, fix it in the other.

#include <array>

namespace np::audio
{

/**
    The four timbres, as data rather than as branches.

    An Omnichord's voice list is short and each entry is a handful of numbers —
    what the oscillator does, how open the filter sits, how long the note wants
    to ring. Keeping them in a table means adding a fifth is a row, and means the
    voice code has no idea which one it is playing.

    The decay scale is a multiplier on the ring length the left trigger sets, not
    a replacement for it: vibes ring longer than a guitar at every trigger
    position, and the trigger still governs.
*/
enum class Waveform { pulse, saw, sine };

struct VoiceSpec
{
    const char* name;
    Waveform    waveform;
    float       pulseWidth;      // ignored unless waveform is pulse
    float       filterMultiple;  // cutoff as a multiple of the note's frequency
    float       decayScale;      // multiplies the ring length
    float       attackSeconds;
    float       detuneCents;
};

inline constexpr int kNumVoices = 4;

inline const VoiceSpec& voiceSpec (int index) noexcept
{
    static const std::array<VoiceSpec, kNumVoices> specs
    {{
        // The one phase 2 shipped: a bright pulse pair, the OM-84's own string.
        { "OMNI",   Waveform::pulse, 0.35f,  7.0f, 1.00f, 0.0020f, 3.0f },

        // Softer and rounder, with the sawtooth's fuller low end.
        { "HARPA",  Waveform::saw,   0.00f,  5.0f, 1.15f, 0.0030f, 2.0f },

        // Nearly a sine, opened wide and left to ring — the bell in the list.
        { "VIBRAFON", Waveform::sine, 0.00f, 12.0f, 1.60f, 0.0040f, 1.5f },

        // Narrow pulse, brighter, and gone sooner. The plucked one.
        { "GITARR", Waveform::pulse, 0.20f,  9.0f, 0.70f, 0.0015f, 4.0f }
    }};

    return specs[(size_t) ((index % kNumVoices) + kNumVoices) % kNumVoices];
}

} // namespace np::audio
