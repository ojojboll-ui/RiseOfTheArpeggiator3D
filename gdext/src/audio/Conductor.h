#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "AcidVoice.h"
#include "Drums.h"
#include "Ensemble.h"
#include "HitChords.h"
#include "JunoVoice.h"
#include "PlingEngine.h"
#include "PluckVoice.h"
#include "Reverb.h"
#include "Riser.h"
#include "Songs.h"
#include "SpscQueue.h"
#include "TempoDelay.h"
#include "VoiceSpec.h"

/**
    The band. Everything musical happens here, on the audio thread, on a clock
    counted in samples.

    **The clock is the game's metronome, not the other way round.** The finest
    grid is a 32nd note, a "tick". Tick k falls on sample round(k × samplesPerTick)
    — computed from k, never accumulated, so the host's buffer size cannot move
    a single event (see block-size-must-change-nothing). The game never decides
    when a note sounds; it says what it wants and the band plays it on the grid:

    - **Chord.** The game publishes which wheel slot the left stick points at.
      It is latched on the next quarter note, so dodging changes the harmony
      on the beat and never between beats.
    - **Fire.** While the right stick is out, the arpeggio runs at 1/8, 1/16 or
      1/32 (the power-up's three steps) and every note it plays is posted back
      as a Shot. The game spawns the bullet when it reads that — the arpeggio
      *is* the gun, so fire rate and note rate cannot disagree.
    - **Hits.** A kill is posted with the chord the bullet was fired on, and
      sounds on the next sixteenth. Kills inside one sixteenth merge into one
      louder chord instead of many: Geometry Wars kills in handfuls, and twenty
      chords in 120 ms is not music.
    - **Layers.** Kick and bass from the start; every eight bars survived adds
      a layer. A death strips one, silences drums and bass until the bar after
      next — a DJ drop — and the game respawns the ship when the music comes
      back in.

    The game reads the band's state from atomics (bar, layer, chord, …) and
    its events from a lock-free queue. Nothing here locks or allocates after
    prepare().
*/
namespace np::audio
{
    struct Command
    {
        enum Type : std::uint8_t { start, stop, pause, resume, kill, death, bomb, powerUp, blip, slow };
        Type  type = blip;
        int   a = 0, b = 0;      // start: song, arp direction. kill: chord slot, size. blip: note. slow: 1 on, 0 off
        float x = 0.5f;          // kill: pan 0..1
        int   note = -1;         // kill: the note of the bullet, if it had one
        int   kind = -1;         // kill: np::EnemyKind, for songs with a sound per kind
    };

    struct Event
    {
        enum Type : std::uint8_t { shot, chord, layerUp, layerDown, bar, dropEnd, songEnd };
        Type type = bar;
        int  a = 0, b = 0, c = 0, d = 0;
        // shot:  a note, b chord slot, c place in the arp cycle 0..100, d accent
        // chord: a slot.   layerUp/Down: a layer.   bar: a bar index
    };

    class Conductor
    {
    public:
        static constexpr int kTicksPerBeat = 8;
        static constexpr int kTicksPerBar  = 32;
        static constexpr int kMaxSegment   = 512;

        //======================================================================
        // Game thread

        std::atomic<int>   requestedChord { 0 };
        std::atomic<bool>  firing { false };
        std::atomic<float> aimPan { 0.5f };

        void post (const Command& c) noexcept { commands.push (c); }
        bool poll (Event& e) noexcept         { return events.pop (e); }

        // Published state. Written by the audio thread, read by anyone.
        std::atomic<std::int64_t> pubSample { 0 };
        std::atomic<double>       pubSamplesPerTick { 2700.0 };
        std::atomic<int>  pubLayer { 0 }, pubChord { 0 }, pubArpRate { 0 }, pubRateBars { 0 };
        std::atomic<int>  pubBarsToLayer { kBarsPerLayer }, pubKicks { 0 }, pubSong { 0 }, pubArpDir { 0 };
        std::atomic<bool> pubRunning { false }, pubDropped { false }, pubEnded { false };
        std::atomic<float> pubPeak { 0.0f };

        /** Which buses reach the output. For measuring one bus alone (the
            offline test does), never touched in the game. */
        enum Bus { busDrums = 1, busBass = 2, busArp = 4, busHits = 8, busPad = 16, busWet = 32, busSlowNoise = 64, busAll = 127 };
        int busMask = busAll;

        /** Beats since the song started, with the fraction — for visuals. */
        double beatPosition() const noexcept
        {
            return (double) pubSample.load() / (pubSamplesPerTick.load() * kTicksPerBeat);
        }

        //======================================================================
        // Audio thread

        void prepare (double sr)
        {
            sampleRate = sr > 0.0 ? sr : 48000.0;

            kit.prepare (sampleRate);
            hits.prepare (sampleRate);
            pling.prepare (sampleRate);
            acid.prepare (sampleRate);
            acid.setSquare (false);
            acid.setGlideSeconds (0.055f);
            acid.setEnvMod (0.7f);
            acid.setDecay (0.3f);

            junoBass.prepare (sampleRate);
            for (auto& p : plucks) p.prepare (sampleRate);
            for (auto& v : pad) v.prepare (sampleRate);
            for (auto& e : padChorus) { e.prepare (sampleRate); e.setRate (0.5f); e.setMix (0.5f); }

            delay.prepare (sampleRate);
            deathEcho.prepare (sampleRate);
            slowEcho.prepare (sampleRate);
            slowEcho.setAmount (1.0f, 0.55f);
            slowNoiseL.set (250.0f, 0.7f, sampleRate);
            slowNoiseR.set (250.0f, 0.7f, sampleRate);
            killEcho.prepare (sampleRate);
            killEcho.setAmount (0.32f, 0.30f);
            deathEcho.setAmount (1.0f, 0.62f);
            echoFall = std::pow (0.001f, 1.0f / (float) (sampleRate * 3.0));
            for (auto& v : arpJuno) v.prepare (sampleRate);
            for (auto& v : killVoices) v.prepare (sampleRate);
            reverb.prepare (sampleRate);
            riser.prepare (sampleRate);
            killPling.prepare (sampleRate);
            for (auto& p : killPlucks) p.prepare (sampleRate);
            killAcid.prepare (sampleRate);
            killAcid.setSquare (true);
            killAcid.setEnvMod (0.85f);
            killAcid.setDecay (0.15f);
            killAcid.setFilter (0.42f, 0.95f * std::pow (0.7f, 0.6f));
            reverb.setRoom (0.82f, 0.4f);

            for (auto* b : { &hitL, &hitR, &arpL, &arpR, &kpL, &kpR })
                b->assign ((size_t) kMaxSegment, 0.0f);

            loadSong (0);
        }

        void render (float* outL, float* outR, int numSamples) noexcept
        {
            drainCommands();

            int pos = 0;

            while (pos < numSamples)
            {
                if (running && ! paused)
                {
                    const auto due = tickSample (nextTick);

                    if (due <= songSample)
                    {
                        doTick (nextTick++);
                        continue;
                    }

                    const auto seg = (int) std::min<std::int64_t> ({ (std::int64_t) (numSamples - pos),
                                                                     due - songSample,
                                                                     (std::int64_t) kMaxSegment });
                    renderSegment (outL + pos, outR + pos, seg);
                    songSample += seg;
                    pos += seg;
                }
                else
                {
                    const auto seg = std::min (numSamples - pos, kMaxSegment);
                    renderSegment (outL + pos, outR + pos, seg);
                    pos += seg;
                }
            }

            publish();
        }

    private:
        //======================================================================
        std::int64_t tickSample (std::int64_t k) const noexcept
        {
            return (std::int64_t) std::llround ((double) k * samplesPerTick);
        }

        void loadSong (int index) noexcept
        {
            songIndex = index;
            current = &song (index);

            samplesPerTick = sampleRate * 60.0 / current->bpm / kTicksPerBeat;
            kit.setTuning (current->drums);
            hits.setBank (current->hitBank);
            junoBass.setPatch (bassPatch (current->bass));
            // A gate (half a step or less) gets the short-release PADV.
            const auto& padSound = ! current->sharpPad ? (current->padRhythm.hold ? padSoftPatch() : padSoftRhythmPatch())
                                 : (current->padRhythm.gate <= 0.5f ? padGatePatch() : padPatch());
            for (auto& v : pad) v.setPatch (padSound);

            delay.setTiming (current->bpm, current->delayDivision);
            delay.setAmount (current->delaySend, current->delayFeedback);
            deathEcho.setTiming (current->bpm, 1);
            slowEcho.setTiming (current->bpm, 2);
            killEcho.setTiming (current->bpm, 1);
            for (auto& v : arpJuno) v.setPatch (arpPluckPatch());
            for (auto& v : killVoices) v.setPatch (killPatch());

            duckLength = (float) (sampleRate * 60.0 / current->bpm * 0.5);
        }

        void startSong (int index, int direction) noexcept
        {
            loadSong (index);
            buildArpCycle (direction);
            riser.stop();
            fillBar = hatFillBar = -1;
            bassIntroStart = bassIntroEnd = -1;
            bassOffset = 0.0f;
            slowOn = pendingBomb = false;
            slowTarget = 0.0f;
            slowOctave = kSlowOpen;
            slowL.reset(); slowR.reset();
            killAcid.release();
            killGateLeft = 0;
            pendingNoteSounds = 0;

            kit.reset();
            hits.allOff();
            acid.release();
            junoBass.noteOff();
            padOff();

            songSample = 0;
            nextTick = 0;
            ticksAlive = 0;
            arpFilterL.reset(); arpFilterR.reset();
            pendingNoteKills = 0;
            pendingNotePan = 0.0f;
            bar = 0;
            layer = 0;
            barsSurvived = 0;
            chord = std::clamp (requestedChord.load(), 0, 3);
            arpRate = 0;
            rateBars = 0;
            arpStep = 0;
            wasFiring = false;
            dropped = false;
            ended = false;
            paused = false;
            running = true;
            fade = fadeTarget = 1.0f;
            sinceKick = 1 << 30;
            bassGateLeft = 0;
            pendingSlide = false;
            for (auto& k : pendingKills) k = {};
        }

        /** The order the arpeggio walks the seven chord tones. Songs with
            randomArp draw a direction at every start; the others keep their
            written eight-step figure. */
        void buildArpCycle (int direction) noexcept
        {
            if (! current->randomArp)
            {
                for (int i = 0; i < 8; ++i) arpCycle[(size_t) i] = current->arpOrder[(size_t) i];
                arpCycleLength = 8;
                arpDirection = 0;
                return;
            }

            arpDirection = direction & 3;
            static constexpr int up[7] { 0, 1, 2, 3, 4, 5, 6 };
            int n = 0;
            switch ((ArpDirection) arpDirection)
            {
                case ArpDirection::up:     for (int i = 0; i < 7; ++i) arpCycle[(size_t) n++] = up[i]; break;
                case ArpDirection::down:   for (int i = 6; i >= 0; --i) arpCycle[(size_t) n++] = up[i]; break;
                case ArpDirection::upDown: for (int i = 0; i < 7; ++i) arpCycle[(size_t) n++] = up[i];
                                           for (int i = 5; i >= 1; --i) arpCycle[(size_t) n++] = up[i]; break;
                case ArpDirection::downUp: for (int i = 6; i >= 0; --i) arpCycle[(size_t) n++] = up[i];
                                           for (int i = 1; i <= 5; ++i) arpCycle[(size_t) n++] = up[i]; break;
            }
            arpCycleLength = n;
        }

        bool partOn (Part p) const noexcept { return layer >= partLayer (*current, p); }

        //======================================================================
        void drainCommands() noexcept
        {
            Command c;

            while (commands.pop (c))
            {
                switch (c.type)
                {
                    case Command::start:   startSong (c.a, c.b); break;
                    case Command::stop:    running = false; fadeTarget = 0.0f; padOff(); slowOn = false; slowTarget = 0.0f; pendingBomb = false; break;
                    case Command::slow:
                        // The game sends how deep slow motion is, 0..1, as its
                        // zoom goes; the filter follows it in octaves, the zoom
                        // being logarithmic too (2026-10-14).
                        slowOn = c.a != 0 && running;
                        slowTarget = slowOn ? std::clamp (c.x, 0.0f, 1.0f) : 0.0f;
                        break;
                    case Command::pause:   if (running) { paused = true; fadeTarget = 0.0f; } break;
                    case Command::resume:  if (paused) { paused = false; fadeTarget = 1.0f; } break;

                    case Command::kill:
                    {
                        if (! running || ended) break;
                        // Which sound: the song's own for this kind of enemy, or
                        // the older rule — the TON note on melody songs, else a chord.
                        KillSound sound = (c.kind >= 0 && c.kind < 6) ? current->killSounds[(size_t) c.kind] : KillSound::automatic;
                        if (sound == KillSound::automatic)
                            sound = (current->hitMelody && c.b < 2) ? KillSound::ton : KillSound::chord;

                        if (sound != KillSound::chord)
                        {
                            const int note = c.note >= 0 ? c.note : arpNote (c.a & 3, pendingNoteSounds % 5);
                            bool dup = false;
                            for (int i = 0; i < pendingNoteSounds; ++i)
                                dup = dup || (pendingNotes[(size_t) i] == note && pendingSounds[(size_t) i] == sound);
                            if (! dup && pendingNoteSounds < (int) pendingNotes.size())
                            {
                                pendingNotes[(size_t) pendingNoteSounds] = note;
                                pendingSounds[(size_t) pendingNoteSounds] = sound;
                                pendingChordOf[(size_t) pendingNoteSounds] = c.a & 3;
                                ++pendingNoteSounds;
                            }
                            ++pendingNoteKills;
                            pendingNotePan += c.x;
                            break;
                        }

                        auto& k = pendingKills[(size_t) (c.a & 3)];
                        k.count++;
                        k.size = std::max (k.size, std::clamp (c.b, 0, 2));
                        k.panSum += c.x;
                        break;
                    }

                    case Command::death:
                        if (! running || ended) break;
                        if (layer > 0) { --layer; postEvent ({ Event::layerDown, layer }); }
                        barsSurvived = 0;
                        dropped = true;
                        dropUntilBar = bar + 2;
                        ticksAlive = 0;
                        riser.stop();
                        fillBar = hatFillBar = -1;
                        bassIntroStart = bassIntroEnd = -1;
                        kit.trigger (PumpKit::boom, 1.0f);
                        kit.trigger (PumpKit::crash, 0.7f);
                        playDeathChord();
                        acid.release();
                        junoBass.noteOff();
                        padOff();
                        // A power-up does not survive a death.
                        arpRate = 0; rateBars = 0;
                        break;

                    case Command::bomb:
                        // The blast is on screen at once; its sound waits for the
                        // next 32nd so it lands in time (2026-10-10).
                        // A swoosh right away, so the button is answered at once;
                        // the bang itself waits for the 32nd (2026-10-13).
                        if (running && ! ended) { pendingBomb = true; kit.trigger (PumpKit::swoosh, 1.0f); }
                        break;

                    case Command::powerUp:
                        arpRate = std::min (2, arpRate + 1);
                        rateBars = kPowerUpBars;
                        kit.trigger (PumpKit::perc, 0.8f);
                        break;

                    case Command::blip:
                        playArpVoice (c.a, 0.55f, c.x);
                        break;
                }
            }
        }

        void postEvent (const Event& e) noexcept { events.push (e); }

        //======================================================================
        void doTick (std::int64_t k) noexcept
        {
            const int t = (int) (k % kTicksPerBar);

            if (t == 0)
                onBar ((int) (k / kTicksPerBar));

            if (! running)
                return;

            if (pendingBomb)
            {
                pendingBomb = false;
                kit.trigger (PumpKit::crash, 1.0f);
                kit.trigger (PumpKit::boom, 0.55f);
                playChord (chord, 2, 1.0f, 0.5f);
                playChord (chord, 0, 0.8f, 0.5f);
            }

            if (t % kTicksPerBeat == 0)
            {
                // The wheel is read here and only here.
                const int want = std::clamp (requestedChord.load (std::memory_order_relaxed), 0, 3);
                if (want != chord)
                {
                    chord = want;
                    postEvent ({ Event::chord, chord });
                }

                if (! dropped)
                {
                    kit.trigger (PumpKit::kick, t == 0 ? 1.0f : 0.92f);
                    sinceKick = 0;
                    kicks++;
                }
            }

            if (t % 2 == 0)
            {
                const int step = t / 2;
                if (! dropped)
                {
                    drumStep (step);
                    padStep (k);
                    if (partOn (Part::bass) || bassIntroAt (k)) bassStep (step);
                }
                flushKills();
            }

            if (hatFillBar == bar && ! dropped && t >= 24 && t % 2 == 1)
                kit.trigger (PumpKit::closedHat, 0.45f + 0.03f * (float) (t - 24));

            sweepArpFilter();
            sweepBass (k);
            sweepAcid (k);
            arpTick (k);
        }

        void onBar (int newBar) noexcept
        {
            bar = newBar;
            postEvent ({ Event::bar, bar });

            if (bar >= current->lengthBars)
            {
                // The end of the song: one last tonic, everything else lets go.
                running = false;
                ended = true;
                acid.release();
                junoBass.noteOff();
                padOff();
                kit.trigger (PumpKit::crash, 1.0f);
                playChord (current->homeSlot, 2, 1.0f, 0.5f);   // the tonic, wherever it sits on the wheel
                playChord (current->homeSlot, 0, 0.7f, 0.5f);
                postEvent ({ Event::songEnd });
                return;
            }

            if (dropped)
            {
                if (bar >= dropUntilBar)
                {
                    dropped = false;
                    kit.trigger (PumpKit::crash, 0.8f);
                    postEvent ({ Event::dropEnd });
                }
            }
            else if (bar > 0 && ++barsSurvived >= kBarsPerLayer)
            {
                barsSurvived = 0;
                if (layer < maxLayer (*current))
                {
                    ++layer;
                    kit.trigger (PumpKit::crash, 0.9f);
                    postEvent ({ Event::layerUp, layer });
                }
            }

            // Build-ups, known a bar or two ahead because the layers arrive on
            // a fixed count: a snare fill through the bar before the clap,
            // a noise riser across the two bars before the pad. A death in
            // between cancels both.
            if (current->buildUps && ! dropped && bar > 0 && layer < maxLayer (*current))
            {
                const Part next = current->arrangement.parts[(size_t) layer];
                const int barsLeft = kBarsPerLayer - barsSurvived;
                if (next == Part::clap && barsLeft == 1) fillBar = bar;
                if (next == Part::sixteenths && barsLeft == 1) hatFillBar = bar;
                if (next == Part::bass && barsLeft == 1)
                {
                    bassIntroStart = (std::int64_t) bar * kTicksPerBar;
                    bassIntroEnd   = (std::int64_t) (bar + 1) * kTicksPerBar;
                }
                if (next == Part::pad && barsLeft == 2)
                    riser.start (tickSample ((std::int64_t) (bar + 2) * kTicksPerBar) - tickSample ((std::int64_t) bar * kTicksPerBar));
            }

            if (rateBars > 0 && --rateBars == 0)
            {
                --arpRate;
                if (arpRate > 0) rateBars = kPowerUpBars;
            }
        }

        void drumStep (int step) noexcept
        {
            const bool offbeat = (step % 4) == 2;

            // Which parts play comes from the song's arrangement (2026-10-09).
            if (partOn (Part::hat) && offbeat)
                kit.trigger (partOn (Part::openHat) ? PumpKit::openHat : PumpKit::closedHat, 0.85f);

            if (partOn (Part::sixteenths) && ! offbeat)
            {
                kit.trigger (PumpKit::closedHat, (step % 2) ? 0.42f : 0.28f);
                if (step % 2) kit.trigger (PumpKit::shaker, 0.5f);
            }

            if (partOn (Part::openHat) && (step == 3 || step == 7 || step == 10 || step == 14))
                kit.trigger (PumpKit::perc, step == 10 ? 0.7f : 0.45f);

            if (partOn (Part::clap) && (step == 4 || step == 12))
                kit.trigger (current->backbeat == Backbeat::clap ? PumpKit::clap : PumpKit::snare, 0.95f);

            // The fill: eighths on beat three, sixteenths on beat four, rising.
            if (hatFillBar == bar && step >= 8)
                kit.trigger (PumpKit::closedHat, 0.35f + 0.06f * (float) (step - 8));

            if (fillBar == bar && step >= 8 && (step >= 12 || step % 2 == 0))
            {
                // Loud enough to be heard over kick and hats: +0.6 dB on the drum
                // bus at 0.35-0.91 was lost; the last two sixteenths add the perc as a tom.
                kit.trigger (PumpKit::snare, 0.75f + 0.09f * (float) (step - 8));
                if (step >= 14) kit.trigger (PumpKit::perc, 1.0f);
            }
        }

        //======================================================================
        int chordThird (int slot) const noexcept { return current->chords[(size_t) slot].minor ? 3 : 4; }

        void bassStep (int step) noexcept
        {
            const auto& s = current->bassPattern[(size_t) step];
            if (! s.on) return;

            const int root = placeAbove (chordRootPc (*current, chord), current->bassLow);
            const int note = root + s.interval;
            const int gate = (int) (s.gate * samplesPerTick * 2.0);

            if (current->bass == BassKind::acid)
            {
                if (pendingSlide && acid.isGateOpen()) acid.glideTo (note, s.accent);
                else                                   acid.trigger (note, s.accent);
                pendingSlide = s.slide;
                bassGateLeft = s.slide ? (int) (samplesPerTick * 2.0 * 1.2) : gate;
            }
            else
            {
                junoBass.noteOn (note, s.accent ? 1.0f : 0.85f, std::max (1, gate), ++order);
            }
        }

        bool bassIntroAt (std::int64_t k) const noexcept { return bassIntroStart >= 0 && k >= bassIntroStart && k < bassIntroEnd; }

        /** Where the bass filter sits against its patch, in slider units
            (0.1 is an octave), set per tick. Zero — the bass where it always
            was — except in the bar before the bass layer, when the bass already
            plays, starting almost shut and opening to its place on the downbeat
            it comes in on. (For a day it also opened with survival like the
            arpeggio; taken out 2026-10-09, and the intro shortened from four
            bars to one.) */
        void sweepBass (std::int64_t k) noexcept
        {
            bassOffset = 0.0f;

            if (bassIntroAt (k))
            {
                const float p = (float) (k - bassIntroStart) / (float) (bassIntroEnd - bassIntroStart);
                bassOffset -= 0.26f * (1.0f - p);
            }
        }

        /** The acid filter breathes over eight bars and opens with the layers.
            Set per tick, not per render segment, so the block size cannot move it. */
        void sweepAcid (std::int64_t k) noexcept
        {
            const double barPos = (double) k / kTicksPerBar;
            const float sweep = 0.5f - 0.5f * (float) std::cos (6.283185307 * barPos / 8.0);
            acid.setFilter (std::clamp (0.20f + 0.16f * sweep + 0.03f * (float) layer + bassOffset, 0.02f, 1.0f),
                            0.95f * std::pow (0.62f, 0.6f));
        }

        /** The arpeggio opens up the longer the ship stays alive: a lowpass
            from 800 Hz to fully open over 48 bars, in octaves so every bar
            brightens it by the same amount to the ear. A death closes it again.
            Set once per tick, so the block size cannot move it. */
        void sweepArpFilter() noexcept
        {
            if (! dropped) ++ticksAlive;
            const float t = std::min (1.0f, (float) ticksAlive / (float) (kArpSweepBars * kTicksPerBar));
            const float hz = 800.0f * std::pow (2.0f, 4.3f * t);
            arpFilterL.set (hz, 0.9f, sampleRate);
            arpFilterR.set (hz, 0.9f, sampleRate);
        }

        void arpTick (std::int64_t k) noexcept
        {
            const bool fire = firing.load (std::memory_order_relaxed) && ! dropped;

            if (fire && ! wasFiring) arpStep = 0;
            wasFiring = fire;
            if (! fire) return;

            static constexpr int divisions[3] { 4, 2, 1 };   // 1/8, 1/16, 1/32 in ticks
            if (k % divisions[arpRate] != 0) return;

            const int index = arpStep % arpCycleLength;
            const int note = arpNote (chord, arpCycle[(size_t) index]);
            const bool accent = index == 0;
            const int place = arpCycleLength > 1 ? index * 100 / (arpCycleLength - 1) : 50;

            // More notes, each softer: at 1/32 the overlapping tails would
            // otherwise make the power-up a volume knob (+5 dB, measured).
            static constexpr float rateLevel[3] { 1.0f, 0.78f, 0.58f };
            playArpVoice (note, (accent ? 0.95f : 0.68f) * rateLevel[arpRate],
                          aimPan.load (std::memory_order_relaxed));
            postEvent ({ Event::shot, note, chord, place, accent ? 1 : 0 });
            ++arpStep;
        }

        int arpNote (int slot, int degree) const noexcept
        {
            const int root = placeAbove (chordRootPc (*current, slot), current->arpLow);
            const int third = chordThird (slot);
            const int tones[7] { 0, third, 7, 12, 12 + third, 19, 24 };
            return root + tones[std::clamp (degree, 0, 6)];
        }

        void playArpVoice (int note, float velocity, float pan) noexcept
        {
            switch (current->arp)
            {
                case ArpKind::pluck:
                {
                    auto* v = &plucks[0];
                    std::uint64_t oldest = ~(std::uint64_t) 0;
                    for (auto& p : plucks)
                    {
                        if (! p.isActive()) { v = &p; break; }
                        if (p.age() < oldest) { oldest = p.age(); v = &p; }
                    }
                    const float hz = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
                    const float ring = std::pow (0.001f, 1.0f / (float) (sampleRate * 0.55));
                    v->start (hz, velocity, ring, voiceSpec (0), 0, ++order);
                    pluckPan = pan;
                    break;
                }

                case ArpKind::plingBox:
                    // Softened 2026-10-07 ("mjukare"): tone 0.10 -> 0.30 thins the clangy
                    // upper partials and the tick, brightness 0.85 -> 0.45 lowers them.
                    pling.trigger (note, velocity, 0.30f, 0.45f, 0.60f, pan);
                    break;

                case ArpKind::plingVibe:
                    pling.trigger (note, velocity, 0.92f, 0.55f, 0.75f, pan);
                    break;

                case ArpKind::junoPluck:
                {
                    size_t slot = 0;
                    std::uint64_t oldest = ~(std::uint64_t) 0;
                    for (size_t k = 0; k < arpJuno.size(); ++k)
                    {
                        if (! arpJuno[k].isActive()) { slot = k; break; }
                        if (arpJuno[k].age() < oldest) { oldest = arpJuno[k].age(); slot = k; }
                    }
                    arpJuno[slot].noteOn (note, velocity, (int) (samplesPerTick * 2.0), ++order);
                    arpJunoPan[slot] = pan;
                    break;
                }
            }
        }

        //======================================================================
        struct PendingKill { int count = 0; int size = 0; float panSum = 0.0f; };

        void flushKills() noexcept
        {
            // At most two chords on one sixteenth, the busiest first.
            for (int played = 0; played < 2; ++played)
            {
                int best = -1;
                for (int s = 0; s < 4; ++s)
                    if (pendingKills[(size_t) s].count > 0
                        && (best < 0 || pendingKills[(size_t) s].count > pendingKills[(size_t) best].count))
                        best = s;

                if (best < 0) break;

                auto& k = pendingKills[(size_t) best];
                // Louder with more kills, in doublings: one kill 0.6, two 0.72,
                // four 0.84, eight and up the ceiling.
                const float v = std::min (1.0f, 0.6f + 0.12f * std::log2 ((float) k.count));
                playChord (best, k.size, v, k.panSum / (float) k.count);
                k = {};
            }

            for (auto& k : pendingKills) k = {};

            // The kill notes: each distinct note and sound once, louder with more kills.
            if (pendingNoteKills > 0)
            {
                const float v = std::min (1.0f, 0.62f + 0.12f * std::log2 ((float) pendingNoteKills));
                const float pan = pendingNotePan / (float) pendingNoteKills;
                for (int i = 0; i < pendingNoteSounds; ++i)
                    playKillSound (pendingNotes[(size_t) i], pendingSounds[(size_t) i], v, pan);
            }
            pendingNoteSounds = pendingNoteKills = 0;
            pendingNotePan = 0.0f;
        }

        static int fold (int note, int low, int high) noexcept
        {
            while (note > high) note -= 12;
            while (note < low)  note += 12;
            return note;
        }

        /** One enemy's death, in its own voice. Song 1's palette (2026-10-09):
            pinwheels a music-box tick, diamonds the Juno TON blip, the green
            dodgers a squelchy 303, splitters a two-note Juno stab, shards a
            high glassy ping, black holes the full chord. */
        void playKillSound (int note, KillSound sound, float velocity, float pan) noexcept
        {
            switch (sound)
            {
                case KillSound::ton:   playKillNote (note, velocity, pan); break;
                // Levels set against TON by tools/audiotest (2026-10-09).
                case KillSound::pling: killPling.trigger (fold (note, 69, 88), velocity * 0.40f, 0.12f, 0.75f, 0.45f, pan); break;
                case KillSound::glass: killPling.trigger (fold (note, 81, 100), velocity * 0.36f, 0.95f, 0.40f, 0.30f, pan); break;
                case KillSound::acid:
                    killAcid.setDecay (0.15f);
                    killAcidGain = 1.0f;
                    killAcid.trigger (fold (note, 45, 64), true);
                    killGateLeft = (int) (samplesPerTick * 3.0);
                    killAcidPan = pan;
                    break;
                case KillSound::stab:
                {
                    const int n = fold (note, 57, 72);
                    playKillNote (n,     velocity * 0.8f, pan, &stabPatch());
                    playKillNote (n + 7, velocity * 0.7f, pan, &stabPatch());
                    break;
                }

                // Songs 2 and 3 (2026-10-09).
                case KillSound::zap:
                    killAcid.setDecay (0.04f);
                    killAcidGain = kZapGain;
                    killAcid.trigger (fold (note, 57, 76), false);
                    killGateLeft = (int) (samplesPerTick * 1.0);
                    killAcidPan = pan;
                    break;
                case KillSound::brass:  playKillNote (note, velocity * kBrassLevel, pan, &brassBlipPatch()); break;
                case KillSound::plock:  playKillNote (note, velocity * kPlockLevel, pan, &arpPluckPatch()); break;
                case KillSound::guitar:
                {
                    const int n = fold (note, 52, 72);
                    playKillPluck (n,     velocity * kGuitarLevel, pan, 3);
                    playKillPluck (n + 7, velocity * kGuitarLevel * 0.85f, pan, 3);
                    break;
                }
                case KillSound::harp:   playKillPluck (fold (note, 55, 74), velocity * kHarpLevel, pan, 1); break;
                case KillSound::vibe:   killPling.trigger (fold (note, 60, 79), velocity * kVibeLevel, 0.92f, 0.55f, 0.50f, pan); break;
                case KillSound::shimmer:
                {
                    const int n = fold (note, 72, 88);
                    killPling.trigger (n,      velocity * kShimmerLevel,         0.92f, 0.70f, 0.60f, pan);
                    killPling.trigger (n + 7,  velocity * kShimmerLevel * 0.8f,  0.92f, 0.70f, 0.60f, 1.0f - pan);
                    killPling.trigger (n + 12, velocity * kShimmerLevel * 0.6f,  0.92f, 0.70f, 0.60f, pan);
                    break;
                }
                case KillSound::bell:   killPling.trigger (fold (note, 84, 100), velocity * kBellLevel, 0.10f, 0.30f, 0.40f, pan); break;
                default: break;
            }
        }

        /** A plucked string from Chordtroller's Omnichord voices, for kills. */
        void playKillPluck (int note, float velocity, float pan, int spec) noexcept
        {
            size_t slot = 0;
            std::uint64_t oldest = ~(std::uint64_t) 0;
            for (size_t k = 0; k < killPlucks.size(); ++k)
            {
                if (! killPlucks[k].isActive()) { slot = k; break; }
                if (killPlucks[k].age() < oldest) { oldest = killPlucks[k].age(); slot = k; }
            }
            const float hz = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
            const float ring = std::pow (0.001f, 1.0f / (float) (sampleRate * 0.40));
            killPlucks[slot].start (hz, velocity, ring, voiceSpec (spec), 0, ++order);
            killPluckPan[slot] = pan;
        }

        /** One kill note, folded into the midrange, on the kill voices — the TON
            patch unless another is given. */
        void playKillNote (int note, float velocity, float pan, const JunoPatch* patch = nullptr) noexcept
        {
            while (note > 76) note -= 12;
            while (note < 57) note += 12;

            size_t slot = 0;
            std::uint64_t oldest = ~(std::uint64_t) 0;
            for (size_t k = 0; k < killVoices.size(); ++k)
            {
                if (! killVoices[k].isActive()) { slot = k; break; }
                if (killVoices[k].age() < oldest) { oldest = killVoices[k].age(); slot = k; }
            }
            killVoices[slot].setPatch (patch != nullptr ? *patch : killPatch());
            killVoices[slot].noteOn (note, velocity, (int) (samplesPerTick * 3.0), ++order);
            killPan[slot] = pan;
        }

        void playChord (int slot, int size, float velocity, float pan) noexcept
        {
            // Lifted from 57/50/43 on 2026-10-07: more of the chord in the midrange.
            static constexpr int lows[3] { 60, 55, 48 };
            const int root = placeAbove (chordRootPc (*current, slot), lows[std::clamp (size, 0, 2)]);
            const int third = chordThird (slot);

            hits.trigger (root,         velocity, size, pan);
            hits.trigger (root + third, velocity, size, pan);
            hits.trigger (root + 7,     velocity, size, pan);
            hits.trigger (root + 12,    velocity * 0.9f, size, pan);
            if (size == 2) hits.trigger (root - 12, velocity, size, pan);

            // The trance song throws a little glass on top of every chord.
            if (current->hitBank == 2)
            {
                pling.trigger (root + 24, velocity * 0.30f, 0.95f, 0.6f, 0.9f, pan);
                pling.trigger (root + 19 + 12, velocity * 0.22f, 0.95f, 0.6f, 0.9f, 1.0f - pan);
            }
        }

        /** The ship's death: the chord it died on, spread over three octaves,
            held long and sent into its own dotted-eighth echo so it rings out
            across the drop. */
        void playDeathChord() noexcept
        {
            const int root = placeAbove (chordRootPc (*current, chord), 36);
            const int third = chordThird (chord);
            const int notes[7] { root, root + 7, root + 12, root + 12 + third, root + 19, root + 24, root + 24 + third };
            for (int n : notes) hits.trigger (n, 1.0f, 2, 0.5f, 2.6f);
            echoSend = 1.0f;
        }

        /** The top layer's pad follows the song's rhythm on the sixteenth
            grid, and each hit takes whatever chord is on the wheel then. */
        void padStep (std::int64_t k) noexcept
        {
            // The pad also plays under the riser that leads into it, rising
            // with the sweep (see renderSegment), so it arrives already moving.
            if (! partOn (Part::pad) && ! riser.isActive()) return;
            const auto& r = current->padRhythm;

            // A held pad changes only when the chord does.
            if (r.hold) { padOn (0); return; }

            const int length = std::max (1, r.beats * 4);
            const int step = (int) ((k / 2) % length);
            if ((r.hits >> step) & 1u) padOn ((int) (r.gate * samplesPerTick * 2.0));
        }

        void padOn (int gateSamples) noexcept
        {
            // Open voicing from G3 up — root, fifth, tenth, twelfth — instead of
            // four close notes from E3, which was half of the mud.
            const int third = chordThird (chord);
            // Songs 1-2 an octave down from 55 since 2026-10-07 ("sänk alla pads en
            // oktav"); song 3 went down too and came back up to 52 the same day.
            const int root = placeAbove (chordRootPc (*current, chord),
                                         (current->sharpPad ? 43 : 52) + 12 * current->padOctave);
            const int open[4]  { root, root + 7, root + 12 + third, root + 19 };
            const int close[4] { root, root + third, root + 7, root + 12 };
            const int* notes = current->sharpPad ? open : close;
            for (int i = 0; i < 4; ++i)
            {
                auto& v = pad[(size_t) i];
                if (gateSamples <= 0)
                {
                    // Held: leave a voice that already holds this note alone.
                    if (v.isGateOn() && v.midiNote() == notes[i]) continue;
                    v.noteOff();
                }
                v.noteOn (notes[i], 0.8f, gateSamples, ++order);
            }
        }

        void padOff() noexcept { for (auto& v : pad) v.noteOff(); }

        //======================================================================
        void renderSegment (float* outL, float* outR, int n) noexcept
        {
            std::fill (hitL.begin(), hitL.begin() + n, 0.0f);
            std::fill (hitR.begin(), hitR.begin() + n, 0.0f);
            std::fill (arpL.begin(), arpL.begin() + n, 0.0f);
            std::fill (arpR.begin(), arpR.begin() + n, 0.0f);

            hits.render (hitL.data(), hitR.data(), n);
            std::fill (kpL.begin(), kpL.begin() + n, 0.0f);
            std::fill (kpR.begin(), kpR.begin() + n, 0.0f);
            killPling.render (kpL.data(), kpR.data(), n);
            pling.render (arpL.data(), arpR.data(), n);

            const float pl = std::cos (pluckPan * 1.5707963f), pr = std::sin (pluckPan * 1.5707963f);
            const float fadeStep = 1.0f / (float) (sampleRate * 0.4);

            static constexpr float bassCutoff[3] { 0.24f, 0.0f, 0.34f };

            // Trims per voice kind, so each song's bass and arpeggio land at the
            // same level against the drums. Measured with tools/audiotest,
            // 2026-10-07: sub -21.6, acid -30.5, roll -29.7 dB RMS untrimmed;
            // pluck -33.9, box -32.7, vibe -25.1.
            static constexpr float bassTrim[3] { 1.0f, 2.8f, 2.5f };
            static constexpr float arpTrim[4]  { 2.8f, 2.1f, 1.1f, 6.4f };   // box 2.5 -> 2.1 after softening; Juno pluck -39.9 dB at 1.5
            const float bTrim = bassTrim[(int) current->bass];
            const float aTrim = arpTrim[(int) current->arp];
            const int bassKindIndex = (int) current->bass;

            // Pad filter: the sharp pad sits brighter with a little resonance;
            // song 3's soft pad where it was (0.50 was the clearer setting it
            // already had, 2026-10-07).
            const float padCutoff = current->sharpPad ? 0.53f : 0.57f;   // sharp: 0.58 was too bright; soft (song 3): 0.50 rumbled
            const float padRes    = current->sharpPad ? 0.14f : 0.12f;
            // kPad was raised 3 dB for the rhythmic pads; a held one is back where it was.
            const float padLevel  = (current->padRhythm.hold ? 0.72f : 1.0f) * current->padGain;

            float peak = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                // Sidechain: the kick pushes everything else down and lets it
                // swell back over an eighth note. That swell is the pump.
                const float d = std::min (1.0f, (float) sinceKick / duckLength);
                const float pump = (1.0f - d) * (1.0f - d);
                if (sinceKick < (1 << 30)) ++sinceKick;

                // Drums -------------------------------------------------------
                float dl = 0.0f, dr = 0.0f;
                kit.process (dl, dr);
                // The riser on its own path, so the sidechain can pump it.
                float rl = 0.0f, rr = 0.0f;
                riser.process (rl, rr);

                // Bass ----------------------------------------------------------
                float bass;
                if (current->bass == BassKind::acid)
                {
                    // A slid note's gate is longer than a step, so it is still open
                    // when the next note arrives and glides into it.
                    if (bassGateLeft > 0 && --bassGateLeft == 0) acid.release();
                    bass = acid.process() * 0.75f * bTrim;
                }
                else
                {
                    bass = junoBass.process (0.0f, std::clamp (bassCutoff[bassKindIndex] + bassOffset, 0.0f, 1.0f), 0.15f, 1.0f) * 0.9f * bTrim;
                }

                // Arp -----------------------------------------------------------
                float pluck = 0.0f;
                for (auto& p : plucks) pluck += p.process();
                float jl = 0.0f, jr = 0.0f;
                if (current->arp == ArpKind::junoPluck)
                    for (size_t k = 0; k < arpJuno.size(); ++k)
                    {
                        if (! arpJuno[k].isActive()) continue;
                        const float x = arpJuno[k].process (0.0f, 0.40f, 0.22f, 1.0f);
                        jl += x * std::cos (arpJunoPan[k] * 1.5707963f);
                        jr += x * std::sin (arpJunoPan[k] * 1.5707963f);
                    }
                float al = (arpL[(size_t) i] + pluck * pl * 0.55f + jl * 0.5f) * aTrim;
                float ar = (arpR[(size_t) i] + pluck * pr * 0.55f + jr * 0.5f) * aTrim;
                arpFilterL.process (al); al = arpFilterL.lp;
                arpFilterR.process (ar); ar = arpFilterR.lp;
                delay.process (al, ar);

                // Pad -----------------------------------------------------------
                float padMono = 0.0f;
                // Under the riser the pad opens with the sweep: the filter climbs
                // from 3.5 octaves below its place and the level from -24 dB,
                // both reaching where the pad lives on the sweep's peak — the
                // downbeat the pad layer comes in on (asked for 2026-10-09).
                float padCut = padCutoff, padGainNow = padLevel;
                if (riser.isActive())
                {
                    const float p = riser.progress();
                    padCut = padCutoff - 0.35f * (1.0f - p);
                    padGainNow *= std::pow (10.0f, -24.0f * (1.0f - p) / 20.0f);
                }
                for (auto& v : pad) if (v.isActive()) padMono += v.process (0.0f, padCut, padRes, 1.0f) * padGainNow;
                float pdl = 0.0f, pdr = 0.0f, unused = 0.0f;
                padChorus[0].process (padMono * 0.3f, pdl, unused);
                padChorus[1].process (padMono * 0.3f, unused, pdr);

                // Slow motion's depth, 0 open to 1 shut, moved first so every
                // part of the effect follows the same sweep.
                {
                    const float target = kSlowOpen - slowTarget * (kSlowOpen - kSlowShut);
                    const float rate = (target < slowOctave ? kSlowDownOct : kSlowUpOct) / (float) sampleRate;
                    slowOctave += std::clamp (target - slowOctave, -rate, rate);
                }
                const float slowDepth = (kSlowOpen - slowOctave) / (kSlowOpen - kSlowShut);

                float hl = hitL[(size_t) i], hr = hitR[(size_t) i];

                // Kill notes, through a small ping-pong echo of their own.
                float kl = 0.0f, kr = 0.0f;
                for (size_t k = 0; k < killVoices.size(); ++k)
                {
                    if (! killVoices[k].isActive()) continue;
                    const float x = killVoices[k].process (0.0f, 0.46f, 0.30f, 1.0f) * kKillTrim;
                    kl += x * std::cos (killPan[k] * 1.5707963f);
                    kr += x * std::sin (killPan[k] * 1.5707963f);
                }
                for (size_t k = 0; k < killPlucks.size(); ++k)
                {
                    if (! killPlucks[k].isActive()) continue;
                    const float x = killPlucks[k].process();
                    kl += x * std::cos (killPluckPan[k] * 1.5707963f);
                    kr += x * std::sin (killPluckPan[k] * 1.5707963f);
                }
                kl += kpL[(size_t) i] * kKillPlingTrim;
                kr += kpR[(size_t) i] * kKillPlingTrim;
                if (killGateLeft > 0 && --killGateLeft == 0) killAcid.release();
                {
                    const float a = killAcid.process() * kKillAcidTrim * killAcidGain;
                    kl += a * std::cos (killAcidPan * 1.5707963f);
                    kr += a * std::sin (killAcidPan * 1.5707963f);
                }
                killEcho.process (kl, kr);
                hl += kl;
                hr += kr;

                // The death chord's echo: the hit bus feeds its own delay while
                // echoSend dies away over three seconds after a death.
                if (echoSend > 1.0e-4f || deathEchoActive)
                {
                    float el = hl * echoSend, er = hr * echoSend;
                    const float inL = el, inR = er;
                    deathEcho.process (el, er);
                    hl += (el - inL) * 0.8f;
                    hr += (er - inR) * 0.8f;
                    echoSend *= echoFall;
                    deathEchoActive = echoSend > 1.0e-4f || std::abs (el - inL) + std::abs (er - inR) > 1.0e-6f;
                }

                // Slow motion echo on the death sounds: a quarter-note echo fed
                // by the hit bus as deep as the slowdown is, its tail left to
                // ring out after it ends (2026-10-10).
                {
                    float el = hl * slowDepth, er = hr * slowDepth;
                    const float inL = el, inR = er;
                    slowEcho.process (el, er);
                    hl += (el - inL) * 0.9f;
                    hr += (er - inR) * 0.9f;
                }

                // Room ------------------------------------------------------------
                float wl = 0.0f, wr = 0.0f;
                // In slow motion everything is sent to the room harder, up to
                // 2.5 times, so the mix sounds big and far away.
                const float send = current->reverbSend * (1.0f + 1.5f * slowDepth);
                reverb.process ((al + hl * 0.8f + pdl * 0.6f) * send,
                                (ar + hr * 0.8f + pdr * 0.6f) * send, wl, wr);

                // Mix ---------------------------------------------------------------
                const float duckBass = 1.0f - 0.55f * pump;
                const float duckArp  = 1.0f - 0.30f * pump;
                const float duckHit  = 1.0f - 0.50f * pump;
                const float duckPad  = 1.0f - current->padDuck * pump;   // 0.65 by default (0.80 until 2026-10-07 lost it under the kick)
                const float duckWet  = 1.0f - 0.50f * pump;
                const float duckRiser = 1.0f - 0.75f * pump;   // the riser pumps hard (2026-10-09)

                const float gDrums = (busMask & busDrums) ? kDrums : 0.0f;
                const float gBass  = (busMask & busBass)  ? kBass * duckBass : 0.0f;
                const float gArp   = (busMask & busArp)   ? kArp * duckArp : 0.0f;
                const float gHits  = (busMask & busHits)  ? kHits * duckHit : 0.0f;
                const float gPad   = (busMask & busPad)   ? kPad * duckPad : 0.0f;
                const float gWet   = (busMask & busWet)   ? kWet * duckWet : 0.0f;

                float l = (dl + rl * duckRiser) * gDrums + bass * gBass + pdl * gPad + wl * gWet;
                float r = (dr + rr * duckRiser) * gDrums + bass * gBass + pdr * gPad + wr * gWet;

                // Slow motion: everything but the arpeggio and the death sounds
                // goes through a resonant lowpass that sweeps down while
                // it is held and back up when it is let go (2026-10-10). In
                // octaves, so the sweep moves evenly to the ear; the resonance
                // grows as it closes. Fully open it is bypassed.
                {
                    if (slowOctave < kSlowOpen - 1.0e-3f)
                    {
                        const float q = 0.75f + 2.2f * slowDepth;   // up to Q 2.95; 3.95 rang too much (2026-10-10)
                        const float hz = std::pow (2.0f, slowOctave);
                        slowL.set (hz, q, sampleRate);
                        slowR.set (hz, q, sampleRate);
                        slowL.process (l); l = slowL.lp;
                        slowR.process (r); r = slowR.lp;
                    }
                    else
                    {
                        slowL.reset(); slowR.reset();
                    }
                }

                // The arpeggio and the hit bus — every enemy's death sound — stay
                // unfiltered: the player's own sound and what it does (2026-10-10).
                l += al * gArp + hl * gHits;
                r += ar * gArp + hr * gHits;

                // The bomb swoosh, after the filter, so it is never muffled.
                {
                    float swl = 0.0f, swr = 0.0f;
                    kit.processSwoosh (swl, swr);
                    l += swl * gDrums;
                    r += swr * gDrums;
                }

                // The dull noise of slow motion: brown noise under a 250 Hz
                // lowpass, faded in with the filter (asked for 2026-10-10).
                if (slowDepth > 1.0e-4f && (busMask & busSlowNoise))
                {
                    brownL = brownL * 0.996f + slowNoise (slowRngL) * 0.04f;
                    brownR = brownR * 0.996f + slowNoise (slowRngR) * 0.04f;
                    slowNoiseL.process (brownL);
                    slowNoiseR.process (brownR);
                    const float g = kSlowNoiseLevel * slowDepth * slowDepth;
                    l += slowNoiseL.lp * g;
                    r += slowNoiseR.lp * g;
                }

                fade += std::clamp (fadeTarget - fade, -fadeStep, fadeStep);

                l = softClip (l * fade);
                r = softClip (r * fade);

                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));

                outL[i] = l;
                outR[i] = r;
            }

            if (peak > peakHold) peakHold = peak;
        }

        static float softClip (float x) noexcept
        {
            // Linear to 0.6, then a tanh knee that never passes 1.
            const float a = std::abs (x);
            if (a <= 0.6f) return x;
            const float y = 0.6f + 0.4f * std::tanh ((a - 0.6f) / 0.4f);
            return x < 0.0f ? -y : y;
        }

        void publish() noexcept
        {
            pubSample.store (songSample);
            pubSamplesPerTick.store (samplesPerTick);
            pubLayer.store (layer);
            pubChord.store (chord);
            pubArpRate.store (arpRate);
            pubRateBars.store (rateBars);
            pubBarsToLayer.store (kBarsPerLayer - barsSurvived);
            pubKicks.store (kicks);
            pubSong.store (songIndex);
            pubArpDir.store (arpDirection);
            pubRunning.store (running && ! paused);
            pubDropped.store (dropped);
            pubEnded.store (ended);
            pubPeak.store (peakHold);
        }

        //======================================================================
        static constexpr int kPowerUpBars = 8;

        // Bus levels, set by measuring each bus alone (tools/audiotest).
        static constexpr float kDrums = 0.56f;
        static constexpr float kBass  = 0.55f;
        static constexpr float kArp   = 0.32f;   // 0.42 until 2026-10-07, "sänk arpeggiot en aning": -2.4 dB
        static constexpr float kHits  = 0.85f;   // 0.60 until 2026-10-07: +3 dB, bursts should be heard
        static constexpr float kPad   = 3.20f;   // 1.60, then 2.30; +3 dB once it became a rhythm (RMS fell 5 dB)
        static constexpr float kWet   = 1.00f;

        SpscQueue<Command, 512>  commands;
        SpscQueue<Event, 2048>   events;

        double sampleRate = 48000.0;
        double samplesPerTick = 2700.0;
        const Song* current = nullptr;
        int songIndex = 0;

        std::int64_t songSample = 0, nextTick = 0;
        int bar = 0, layer = 0, barsSurvived = 0, chord = 0;
        int arpRate = 0, rateBars = 0, arpStep = 0, kicks = 0, dropUntilBar = 0;
        bool running = false, paused = false, dropped = false, ended = false, wasFiring = false;
        float fade = 0.0f, fadeTarget = 0.0f, peakHold = 0.0f;
        int sinceKick = 1 << 30;
        float duckLength = 10000.0f;

        PumpKit kit;
        HitChords hits;
        PlingEngine pling;
        AcidVoice acid;
        int bassGateLeft = 0;
        bool pendingSlide = false;
        JunoVoice junoBass;
        std::array<PluckVoice, 8> plucks;
        float pluckPan = 0.5f;
        std::array<JunoVoice, 4> pad;
        std::array<Ensemble, 2> padChorus;
        TempoDelay delay, deathEcho, killEcho, slowEcho;
        Svf slowNoiseL, slowNoiseR;
        float brownL = 0.0f, brownR = 0.0f;
        std::uint32_t slowRngL = 0x68e31da4u, slowRngR = 0xb5297a4du;
        static constexpr float kSlowNoiseLevel = 0.12f;   // -30 dB RMS alone; 0.30 gave -22, too present
        static float slowNoise (std::uint32_t& s) noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (std::int32_t) s * (1.0f / 2147483648.0f);
        }
        float echoSend = 0.0f, echoFall = 0.9999f;
        bool deathEchoActive = false;
        std::array<JunoVoice, 8> arpJuno;
        std::array<JunoVoice, 8> killVoices;
        std::array<float, 8> killPan {};
        std::array<int, 6> pendingNotes {}, pendingChordOf {};
        std::array<KillSound, 6> pendingSounds {};
        int pendingNoteSounds = 0, pendingNoteKills = 0;
        PlingEngine killPling;
        std::array<PluckVoice, 6> killPlucks;
        std::array<float, 6> killPluckPan {};
        // Kill sound levels against TON, measured by tools/audiotest.
        static constexpr float kBrassLevel = 0.57f, kPlockLevel = 0.66f, kGuitarLevel = 0.105f, kHarpLevel = 0.66f;
        static constexpr float kVibeLevel = 0.27f, kShimmerLevel = 0.165f, kBellLevel = 0.46f, kZapGain = 1.9f;
        float killAcidGain = 1.0f;
        AcidVoice killAcid;
        int killGateLeft = 0;
        float killAcidPan = 0.5f;
        static constexpr float kKillPlingTrim = 1.0f;
        static constexpr float kKillAcidTrim  = 0.17f;
        Riser riser;
        bool pendingBomb = false;

        // Slow motion's master lowpass, as log2 of its cutoff in hertz.
        static constexpr float kSlowOpen = 14.2f;      // about 19 kHz: out of the way
        static constexpr float kSlowShut = 8.9f;       // about 480 Hz
        // Fast enough to land with the zoom, which is most of the way there in
        // about a quarter of a second (0.4 s and 0.6 s felt late, 2026-10-13).
        static constexpr float kSlowDownOct = 45.0f;   // octaves per second closing: ~75 ms from the jump
        static constexpr float kSlowUpOct   = 24.0f;   // and opening: ~0.22 s
        bool slowOn = false;
        float slowTarget = 0.0f;   // 0 open .. 1 shut, from the game
        float slowOctave = kSlowOpen;
        Svf slowL, slowR;
        int fillBar = -1, hatFillBar = -1;
        std::int64_t bassIntroStart = -1, bassIntroEnd = -1;
        float bassOffset = 0.0f;
        std::array<int, 16> arpCycle {};
        int arpCycleLength = 8, arpDirection = 0;
        float pendingNotePan = 0.0f;
        static constexpr float kKillTrim = 0.75f;   // 0.6 until 2026-10-07: +2 dB
        static constexpr int kArpSweepBars = 48;
        int ticksAlive = 0;
        Svf arpFilterL, arpFilterR;
        std::array<float, 8> arpJunoPan {};
        Reverb reverb;
        std::uint64_t order = 0;

        std::array<PendingKill, 4> pendingKills {};
        std::vector<float> hitL, hitR, arpL, arpR, kpL, kpR;
    };
}
