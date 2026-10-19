#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

#include "Drums.h"
#include "JunoVoice.h"

/**
    The three songs, as data.

    **One wheel layout for all three.** Every song is in a minor key and puts
    the same four functions in the same four directions: i straight up, VI to
    the right, III down, VII to the left. Going round clockwise is i–VI–III–VII,
    the progression half of dance music is built on, so circling the stick plays
    the song and stopping anywhere is somewhere that belongs. Chordtroller's
    rule, carried over: there is only ever one layout to learn. The songs differ
    in key, tempo and sound — never in where the chords are.

    Bass, arpeggio and the hit chords all follow whichever chord the wheel is
    on. A step pattern only says *which chord tone* and *when*; the chord says
    what that is.
*/
namespace np::audio
{
    enum class BassKind { junoSub, acid, junoRoll };
    enum class ArpKind  { pluck, plingBox, plingVibe, junoPluck };
    enum class Backbeat { clap, snare };

    /** What joins the track at each layer after the first; the kick is always
        there, and anything not listed (the bass, on songs 2-3) is there from
        the start. Per song since 2026-10-09, when song 1 began with the kick
        alone. */
    enum class Part { bass, hat, sixteenths, openHat, pad, clap };
    struct Arrangement { int count; std::array<Part, 6> parts; };

    /** How each kind of enemy sounds when it dies, indexed like np::EnemyKind:
        wanderer, seeker, dodger, splitter, shard, well. `automatic` keeps the
        older rule: the TON note on melody songs, the chord otherwise. */
    enum class KillSound { automatic, ton, pling, acid, stab, glass, chord,
                           zap, brass, guitar, vibe, plock, harp, shimmer, bell };

    enum class ArpDirection { up, down, upDown, downUp };

    inline const char* arpDirectionName (int d) noexcept
    {
        static const char* names[4] { "UP", "DOWN", "UP-DOWN", "DOWN-UP" };
        return names[d & 3];
    }

    struct ChordSlot { int offset; bool minor; };

    struct BassStep
    {
        bool  on       = false;
        int   interval = 0;       // semitones above the chord root: 0, 7, 10 or 12
        bool  accent   = false;
        bool  slide    = false;   // 303 only: glide into the next note
        float gate     = 0.5f;    // in sixteenths
    };

    struct Song
    {
        const char* name;
        const char* tagline;
        double bpm;
        int    keyRoot;                         // pitch class, C = 0
        std::array<ChordSlot, 4> chords;        // up, right, down, left

        BassKind bass;
        int      bassLow;                       // lowest MIDI note a bass root may take
        std::array<BassStep, 16> bassPattern;

        ArpKind  arp;
        int      arpLow;                        // lowest MIDI note of the arp's root
        std::array<int, 8> arpOrder;            // indices into r,3,5,r',3',5',r''

        Backbeat backbeat;
        DrumTuning drums;
        int      hitBank;
        int      lengthBars;
        float    delaySend, delayFeedback;
        int      delayDivision;                 // TempoDelay: 0 eighth, 1 dotted, 2 quarter
        float    reverbSend;

        /** Kills play the note of the bullet that made them, on the Juno TON
            voice in the midrange, instead of a chord — so a run of kills is a
            loose melody out of the arpeggio (asked for 2026-10-07, songs 1-2).
            Large enemies still let out their chord. */
        bool     hitMelody = false;

        /** The sharper pad (PADV, open voicing) — songs 1 and 2. Song 3 keeps
            the original PAD as Oloff asked, 2026-10-07. */
        bool     sharpPad = false;

        /** How the top layer's pad is played, borrowed from Chordtroller's
            JunoPhrases (2026-10-07): sixteenth steps over `beats` beats, one
            bit per step, gate in steps. */
        struct PadRhythm { int beats = 1; std::uint32_t hits = 0x1; float gate = 0.8f; bool hold = false; };
        PadRhythm padRhythm {};

        int   padOctave = 0;            // octaves from the pad's usual register
        float padGain   = 1.0f;         // per-song pad trim on top of kPad

        Arrangement arrangement { 5, {{ Part::hat, Part::sixteenths, Part::openHat, Part::pad, Part::clap }} };
        bool randomArp = false;         // up, down, up-down or down-up, drawn at every start
        bool buildUps  = false;         // a snare fill before the clap, a noise riser before the pad
        std::array<KillSound, 6> killSounds {};

        /** How deep the kick ducks the pad, 0..1. */
        float padDuck = 0.65f;

        /** The wheel slot the song starts on: its tonic, wherever that sits. */
        int homeSlot = 0;
    };

    inline constexpr int kNumSongs = 3;

    /** Bars of survival per extra layer. Eight bars is one phrase. */
    inline constexpr int kBarsPerLayer = 8;

    inline const Song& song (int index) noexcept
    {
        // Shorthands for the step tables: X a plain note, A accented, S slid.
        constexpr BassStep _ {};
        constexpr auto X  = [] (int iv, float g) { return BassStep { true, iv, false, false, g }; };
        constexpr auto A  = [] (int iv, float g) { return BassStep { true, iv, true,  false, g }; };
        constexpr auto S  = [] (int iv, float g) { return BassStep { true, iv, false, true,  g }; };

        static const std::array<Song, kNumSongs> songs
        {{
            // 1 -------------------------------------------------------------
            // House. The bass sits on the off-beats, so kick and bass take turns
            // and the pump is in the gaps between them.
            { "PULSE", "JUNO HOUSE", 122.0, 9,
              {{ { 0, true }, { 8, false }, { 3, false }, { 10, false } }},     // Am F C G
              BassKind::junoSub, 33,
              {{ _, _, X(0, 1.5f), _,   _, _, X(0, 1.5f), _,
                 _, _, X(0, 1.5f), _,   _, _, X(0, 1.2f), X(12, 0.5f) }},
              ArpKind::pluck, 64, {{ 0, 1, 2, 3, 4, 5, 6, 3 }},
              Backbeat::clap,
              { 47.0f, 160.0f, 0.34f, 1.8f, 0.40f, 1150.0f, 185.0f, 1.00f, 520.0f, 1.0f },
              0, 80, 0.22f, 0.38f, 1, 0.22f, true, true,             // 80 bars: a minute shorter (2026-10-09)
              { 4, 0x1249u, 0.50f },                                     // GATE PUNKT: steps 0 3 6 9 12 over four beats
              -1, 1.26f,                                                 // an octave lower and +2 dB, 2026-10-08
              { 6, {{ Part::bass, Part::hat, Part::sixteenths, Part::openHat, Part::pad, Part::clap }} },   // the kick alone first
              true, true,
              {{ KillSound::pling, KillSound::ton, KillSound::acid, KillSound::stab, KillSound::glass, KillSound::chord }} },

            // 2 -------------------------------------------------------------
            // Acid. A sixteenth-note 303 with accents and slides, a harder kick,
            // a snare instead of a clap, and stabs for the hits.
            { "ACID", "303 ACID", 130.0, 4,
              {{ { 0, true }, { 8, false }, { 3, false }, { 10, false } }},     // Em C G D
              BassKind::acid, 36,
              {{ A(0, 0.6f), _,           X(12, 0.5f), S(0, 1.0f),
                 X(0, 0.5f), _,           A(12, 0.6f), X(7, 0.5f),
                 X(0, 0.5f), S(0, 1.0f),  X(12, 0.5f), _,
                 A(0, 0.6f), X(7, 0.5f),  S(12, 1.0f), X(0, 0.5f) }},
              ArpKind::junoPluck, 62, {{ 0, 2, 1, 3, 2, 4, 3, 5 }},
              Backbeat::snare,
              { 45.0f, 190.0f, 0.28f, 2.4f, 0.55f, 1150.0f, 190.0f, 1.08f, 640.0f, 1.0f },
              1, 80, 0.18f, 0.30f, 0, 0.16f, true, true,             // 80 bars: a minute shorter (2026-10-09)
              { 2, 0x49u, 1.20f },                                       // SYNKOP 3-3-2: steps 0, 3, 6
              0, 1.0f,
              { 6, {{ Part::bass, Part::hat, Part::sixteenths, Part::openHat, Part::pad, Part::clap }} },   // the kick alone first
              true, true,
              // pinwheel 303 zap, diamond horn blip, dodger 303 squelch,
              // splitter two-note guitar pluck, shard glass, well chord
              {{ KillSound::zap, KillSound::brass, KillSound::acid, KillSound::guitar, KillSound::glass, KillSound::chord }} },

            // 3 -------------------------------------------------------------
            // Trance. A rolling bass on every sixteenth the kick leaves free,
            // vibraphone arpeggio into a long dotted-eighth delay, pad hits.
            { "STARFALL", "TRANCE", 138.0, 6,
              // E F#m Bm A: E up and Bm down (2026-10-13), the wheel turned one step
              // so going clockwise is still F#m Bm A E. Bm replaced D, chosen by ear.
              {{ { 10, false }, { 0, true }, { 5, true }, { 3, false } }},
              BassKind::junoRoll, 38,
              {{ _, X(0, 0.75f), X(0, 0.75f), X(12, 0.75f),
                 _, X(0, 0.75f), X(0, 0.75f), X(12, 0.75f),
                 _, X(0, 0.75f), X(0, 0.75f), X(12, 0.75f),
                 _, X(0, 0.75f), X(7, 0.75f), X(12, 0.75f) }},
              ArpKind::plingVibe, 62, {{ 0, 3, 1, 4, 2, 5, 6, 3 }},
              Backbeat::clap,
              { 50.0f, 150.0f, 0.38f, 2.0f, 0.45f, 1300.0f, 185.0f, 0.95f, 440.0f, 1.0f },
              2, 80, 0.30f, 0.48f, 1, 0.30f, false, false,           // 80 bars: about a minute shorter (2026-10-09)
              // Held again after a day as the off-beat stab, which pumped too
              // hard (2026-10-09); the pump now comes from a deeper sidechain.
              { 1, 0x1u, 0.0f, true },
              0, 1.2f,                                                   // +1.6 dB: makes up the level the sub octave took with it
              { 6, {{ Part::bass, Part::hat, Part::sixteenths, Part::openHat, Part::pad, Part::clap }} },   // the kick alone first
              true, true,
              // pinwheel vibraphone, diamond Juno pluck, dodger harp,
              // splitter a glassy three-note shimmer, shard a high bell, well chord
              {{ KillSound::vibe, KillSound::plock, KillSound::harp, KillSound::shimmer, KillSound::bell, KillSound::chord }},
              0.85f,                                                     // held pad, ducked deeper than the rest so it pumps
              1 }                                                        // starts on F#m, now to the right
        }};

        return songs[(size_t) ((index % kNumSongs + kNumSongs) % kNumSongs)];
    }

    inline int maxLayer (const Song& s) noexcept { return s.arrangement.count; }

    /** The layer at which a part comes in; 0 if it is there from the start. */
    inline int partLayer (const Song& s, Part p) noexcept
    {
        for (int i = 0; i < s.arrangement.count; ++i)
            if (s.arrangement.parts[(size_t) i] == p) return i + 1;
        return 0;
    }

    inline const char* partName (Part p) noexcept
    {
        switch (p)
        {
            case Part::bass:       return "BASS";
            case Part::hat:        return "HI-HAT";
            case Part::sixteenths: return "16THS";
            case Part::openHat:    return "OPEN HAT";
            case Part::pad:        return "PAD";
            case Part::clap:       return "CLAP";
        }
        return "";
    }

    inline const char* layerName (const Song& s, int layer) noexcept
    {
        if (layer <= 0) return partLayer (s, Part::bass) == 0 ? "KICK + BASS" : "KICK";
        return partName (s.arrangement.parts[(size_t) std::min (layer, s.arrangement.count) - 1]);
    }

    inline int chordRootPc (const Song& s, int slot) noexcept
    {
        return (s.keyRoot + s.chords[(size_t) (slot & 3)].offset) % 12;
    }

    /** "Am", "F", "C#m" — what the wheel prints. */
    inline void chordName (const Song& s, int slot, char* out, int size) noexcept
    {
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf (out, (size_t) size, "%s%s", names[chordRootPc (s, slot)],
                       s.chords[(size_t) (slot & 3)].minor ? "m" : "");
    }

    /** The lowest MIDI note at or above `low` with this pitch class. */
    inline int placeAbove (int pitchClass, int low) noexcept
    {
        int n = low + ((pitchClass - low) % 12 + 12) % 12;
        return n;
    }

    /** Juno patches for the two Juno basses. Same columns as JunoPatch. */
    inline const JunoPatch& bassPatch (BassKind kind) noexcept
    {
        // name      saw   pul   sub   noise  pw    pwm   lfo    env   key   hpf    A      D     S     R     lvl   dly  fb   div
        static const JunoPatch sub  { "SUB",  0.30f,0.00f,1.00f,0.00f, 0.50f,0.00f,0.50f, 0.30f,0.20f,0.00f, 0.002f,0.30f,0.55f,0.06f, 0.95f, 0.0f,0.0f,0 };
        static const JunoPatch roll { "ROLL", 1.00f,0.20f,0.50f,0.00f, 0.50f,0.00f,0.50f, 0.45f,0.30f,0.02f, 0.002f,0.14f,0.20f,0.05f, 0.85f, 0.0f,0.0f,0 };
        return kind == BassKind::junoRoll ? roll : sub;
    }

    /** Chordtroller's PLOCK, for the acid song's arpeggio (asked for 2026-10-07:
        "nåt juno plock" instead of the music box). */
    inline const JunoPatch& arpPluckPatch() noexcept
    {
        static const JunoPatch p { "PLOCK", 0.20f,0.80f,0.20f,0.00f, 0.30f,0.10f,0.60f, 0.70f,0.40f,0.08f, 0.002f,0.35f,0.00f,0.30f, 0.75f, 0.0f,0.0f,0 };
        return p;
    }

    /** The top layer's pad. Started as Chordtroller's PAD and was too thick
        under a kick and a bass ("grötig", 2026-10-07): the sub octave is gone,
        the Juno's HPF is up to about 150 Hz, the saw leads and the attack is
        quicker, so it has an edge instead of a cloud. */
    inline const JunoPatch& padPatch() noexcept
    {
        // Between the old PAD and the first PADV, which was too bright: HPF
        // 0.45 (about 100 Hz), saw eased back. Attack 8 ms and release 0.25 s
        // since the pad became a rhythm — a stab cannot have a slow swell.
        static const JunoPatch pad { "PADV", 0.85f,0.40f,0.00f,0.00f, 0.50f,0.30f,0.45f, 0.28f,0.30f,0.45f, 0.008f,0.60f,0.65f,0.25f, 0.60f, 0.0f,0.0f,0 };
        return pad;
    }

    /** PADV with a 60 ms release, for a pad played as a gate: the gaps are the
        rhythm, so the tail must not fill them. */
    inline const JunoPatch& padGatePatch() noexcept
    {
        // Release 0.06 -> 0.40 s on 2026-10-08: Oloff wanted a longer tail.
        static const JunoPatch pad { "PADV-G", 0.85f,0.40f,0.00f,0.00f, 0.50f,0.30f,0.45f, 0.28f,0.30f,0.45f, 0.008f,0.60f,0.65f,0.40f, 0.60f, 0.0f,0.0f,0 };
        return pad;
    }

    /** Chordtroller's PAD, as it was — song 3's. */
    inline const JunoPatch& padSoftPatch() noexcept
    {
        // Held again since the trance gate came off (2026-10-07), so its own
        // slow attack and long release are back.
        // Sub octave off since 2026-10-08: it was what rumbled.
        static const JunoPatch pad { "PAD", 0.80f,0.45f,0.00f,0.00f, 0.50f,0.35f,0.45f, 0.22f,0.30f,0.05f, 0.55f,1.20f,0.80f,1.40f, 0.55f, 0.0f,0.0f,0 };
        return pad;
    }

    /** Chordtroller's STOT, short: the splitter's two-note stab. */
    inline const JunoPatch& stabPatch() noexcept
    {
        static const JunoPatch p { "STOT", 0.90f,0.25f,0.40f,0.00f, 0.50f,0.10f,0.80f, 0.60f,0.40f,0.05f, 0.002f,0.22f,0.20f,0.20f, 0.72f, 0.0f,0.0f,0 };
        return p;
    }

    /** Chordtroller's BRASS, cut short: a horn blip. */
    inline const JunoPatch& brassBlipPatch() noexcept
    {
        static const JunoPatch p { "BRASS", 1.00f,0.00f,0.20f,0.00f, 0.50f,0.00f,0.50f, 0.55f,0.35f,0.10f, 0.012f,0.30f,0.30f,0.20f, 0.70f, 0.0f,0.0f,0 };
        return p;
    }

    /** Song 3's PAD with a quick attack and a short release, for when it is
        played as a rhythm instead of held. */
    inline const JunoPatch& padSoftRhythmPatch() noexcept
    {
        static const JunoPatch pad { "PAD-R", 0.80f,0.45f,0.00f,0.00f, 0.50f,0.35f,0.45f, 0.22f,0.30f,0.05f, 0.006f,1.20f,0.80f,0.30f, 0.55f, 0.0f,0.0f,0 };
        return pad;
    }

    /** The kill note: a short pulse-and-saw blip with some bite in the mids. */
    inline const JunoPatch& killPatch() noexcept
    {
        static const JunoPatch p { "TON", 0.55f,0.60f,0.25f,0.00f, 0.40f,0.20f,0.50f, 0.50f,0.35f,0.20f, 0.002f,0.30f,0.10f,0.35f, 0.80f, 0.0f,0.0f,0 };
        return p;
    }
}
