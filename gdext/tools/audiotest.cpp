// Offline sweep of the band: every song, scripted play, no window and no pad.
//
//   build.bat audiotest     (from tools/, after vcvars64)
//   audiotest.exe [outdir]
//
// Asserts: no NaN/INF, no clipping past the soft clip, every song reaches the
// top layer, a death drops a layer and the drop ends, shots come at the grid
// rate — and that rendering at 256 and at 128 samples per block gives the same
// samples and the same events. Writes one WAV per song to listen to.

#include "../src/audio/Conductor.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace np::audio;

namespace
{
    constexpr double kRate = 48000.0;

    struct Run
    {
        std::vector<float> left, right;
        std::vector<Event> events;
        std::vector<std::int64_t> eventBlock;
    };

    // Input is driven off a clock in samples, delivered only at multiples of
    // 256 so both block sizes see each command at the same sample.
    Run play (int songIndex, int blockSize, double seconds, int busMask = Conductor::busAll)
    {
        auto c = std::make_unique<Conductor>();
        c->prepare (kRate);
        c->busMask = busMask;

        Run run;
        const auto total = (std::int64_t) (seconds * kRate);
        run.left.resize ((size_t) total);
        run.right.resize ((size_t) total);

        c->requestedChord = 0;
        c->post ({ Command::start, songIndex });

        std::uint32_t rng = 12345u;
        auto rnd = [&rng] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };

        std::int64_t pos = 0;
        std::int64_t nextInput = 0;

        while (pos < total)
        {
            if (pos >= nextInput)
            {
                const double t = (double) pos / kRate;

                // Circle the wheel, one slot every ~2.3 s.
                c->requestedChord = (int) (t / 2.3) % 4;

                // Fire in bursts: 3 s on, 1 s off.
                c->firing = std::fmod (t, 4.0) < 3.0;
                c->aimPan = 0.5f + 0.4f * (float) std::sin (t);

                // A few kills per second, in clumps.
                // None during the death drop, as in the game, where a death clears the field.
                if (rnd() < 0.08f && (t < 94.0 || t > 101.0))
                {
                    const int n = 1 + (int) (rnd() * 4.0f);
                    for (int i = 0; i < n; ++i)
                        c->post ({ Command::kill, (int) (rnd() * 4.0f), (int) (rnd() * 3.0f), rnd(), -1, (int) (rnd() * 6.0f) });
                }

                // One-off events, on the first input slot at or after their time.
                auto at = [pos] (double when)
                {
                    const auto s = (std::int64_t) (when * kRate);
                    return pos >= s && pos - s < 256;
                };
                if (at (20.0))  c->post ({ Command::powerUp });
                if (at (24.0))  c->post ({ Command::powerUp });
                if (at (95.0))  c->post ({ Command::death });
                if (at (110.0)) c->post ({ Command::bomb });
                if (at (40.0))  c->post ({ Command::slow, 1, 0, 1.0f });
                if (at (44.0))  c->post ({ Command::slow, 0 });
                if (at (60.0))  c->post ({ Command::slow, 1, 0, 1.0f });
                if (at (60.7))  c->post ({ Command::slow, 0 });
                if (at (70.0))  c->post ({ Command::bomb });

                nextInput += 256;
            }

            const int n = (int) std::min<std::int64_t> (blockSize, total - pos);
            c->render (run.left.data() + pos, run.right.data() + pos, n);

            Event e;
            while (c->poll (e)) { run.events.push_back (e); run.eventBlock.push_back (pos); }

            pos += n;
        }

        return run;
    }

    void writeWav (const std::string& path, const Run& r)
    {
        FILE* f = std::fopen (path.c_str(), "wb");
        if (! f) { std::printf ("  cannot write %s\n", path.c_str()); return; }

        const std::uint32_t frames = (std::uint32_t) r.left.size();
        const std::uint32_t dataBytes = frames * 4;
        auto u32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };

        std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 ((std::uint32_t) kRate);
        u32 ((std::uint32_t) kRate * 4); u16 (4); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (dataBytes);

        for (std::uint32_t i = 0; i < frames; ++i)
        {
            const std::int16_t l = (std::int16_t) std::lround (std::clamp (r.left[i],  -1.0f, 1.0f) * 32767.0f);
            const std::int16_t rr = (std::int16_t) std::lround (std::clamp (r.right[i], -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&l, 2, 1, f);
            std::fwrite (&rr, 2, 1, f);
        }

        std::fclose (f);
    }

    double dB (double x) { return 20.0 * std::log10 (std::max (1.0e-9, x)); }
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    int failures = 0;
    auto fail = [&failures] (const char* what) { std::printf ("  FAIL: %s\n", what); ++failures; };

    for (int s = 0; s < kNumSongs; ++s)
    {
        const auto& sg = song (s);
        const double seconds = 125.0;
        std::printf ("%s (%s, %.0f BPM)\n", sg.name, sg.tagline, sg.bpm);

        const auto a = play (s, 256, seconds);
        const auto b = play (s, 128, seconds);

        // Health -----------------------------------------------------------
        bool finite = true;
        double peak = 0.0;
        for (size_t i = 0; i < a.left.size(); ++i)
        {
            if (! std::isfinite (a.left[i]) || ! std::isfinite (a.right[i])) { finite = false; break; }
            peak = std::max (peak, (double) std::max (std::abs (a.left[i]), std::abs (a.right[i])));
        }
        if (! finite) fail ("NaN or INF in output");
        if (peak > 1.0) fail ("output past full scale");

        std::printf ("  peak %.1f dBFS\n", dB (peak));
        std::printf ("  RMS per 10 s:");
        for (size_t start = 0; start + 480000 <= a.left.size(); start += 480000)
        {
            double sum = 0.0;
            for (size_t i = start; i < start + 480000; ++i) sum += a.left[i] * a.left[i] + a.right[i] * a.right[i];
            std::printf (" %.1f", dB (std::sqrt (sum / 960000.0)));
        }
        std::printf ("\n");

        // The death at 95 s: the hit bus alone, half a second at a time, so the
        // death chord and its echo can be seen ringing out and dying away.
        {
            const auto h = play (s, 256, 101.0, Conductor::busHits);
            std::printf ("  hits after death:");
            for (double t = 94.5; t + 0.5 <= 101.0; t += 0.5)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (t * kRate), a1 = (size_t) ((t + 0.5) * kRate);
                for (size_t i = a0; i < a1; ++i) sum += h.left[i] * h.left[i] + h.right[i] * h.right[i];
                std::printf (" %.0f", dB (std::sqrt (sum / (2.0 * (a1 - a0)))));
            }
            std::printf ("\n");
        }

        // Each enemy kind's death sound alone (songs with a sound per kind):
        // one kill every 0.3 s for 15 s, hit bus only, at the top layer's
        // levels. Kinds: wanderer seeker dodger splitter shard well.
        if (song (s).killSounds[0] != KillSound::automatic)
        {
            static const char* kinds[6] { "wanderer", "seeker", "dodger", "splitter", "shard", "well" };
            static const int sizes[6] { 0, 1, 1, 1, 0, 2 };
            std::printf ("  kill sounds (RMS/peak dB):");
            for (int kind = 0; kind < 6; ++kind)
            {
                auto c = std::make_unique<Conductor>();
                c->prepare (kRate);
                c->busMask = Conductor::busHits;
                c->post ({ Command::start, s });
                std::vector<float> l (256), r (256);
                double sum = 0.0, pk = 0.0; size_t count = 0;
                std::uint32_t seed = 99u;
                for (std::int64_t pos = 0; pos < (std::int64_t) (15.0 * kRate); pos += 256)
                {
                    if (pos % (std::int64_t) (0.3 * kRate) < 256)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        c->post ({ Command::kill, (int) (seed >> 30), sizes[kind], (float) ((seed >> 8) & 255) / 255.0f,
                                   60 + (int) ((seed >> 16) % 24u), kind });
                    }
                    c->render (l.data(), r.data(), 256);
                    for (int i = 0; i < 256; ++i)
                    {
                        sum += l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i];
                        pk = std::max (pk, (double) std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
                    }
                    count += 512;
                    Event e; while (c->poll (e)) {}
                }
                std::printf (" %s %.1f/%.1f", kinds[kind], dB (std::sqrt (sum / (double) count)), dB (pk));
            }
            std::printf ("\n");
        }

        // Build-ups: the drum bus in the two bars before the pad (riser) and the
        // second half of the bar before the clap (fill), against the same span
        // one phrase earlier.
        if (song (s).buildUps)
        {
            const auto dr = play (s, 256, 100.0, Conductor::busDrums);
            const double barSec = 240.0 / song (s).bpm;
            auto rms = [&dr, barSec] (double barFrom, double barTo)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (barFrom * barSec * kRate), a1 = (size_t) (barTo * barSec * kRate);
                for (size_t i = a0; i < a1; ++i) sum += dr.left[i] * dr.left[i] + dr.right[i] * dr.right[i];
                return dB (std::sqrt (sum / (2.0 * (a1 - a0))));
            };
            const int padBar = partLayer (song (s), Part::pad) * kBarsPerLayer;
            const int clapBar = partLayer (song (s), Part::clap) * kBarsPerLayer;
            std::printf ("  build-ups (drum bus dB): riser bars %d-%d %.1f vs %.1f a phrase before;"
                         " fill bar %d second half %.1f vs %.1f\n",
                         padBar - 2, padBar - 1, rms (padBar - 2, padBar), rms (padBar - 10, padBar - 8),
                         clapBar - 1, rms (clapBar - 0.5, clapBar), rms (clapBar - 2.5, clapBar - 2));

            // The bass intro: a bar at a time from bar 3, the bass layer coming
            // in at bar 8; then the bass early in its life against late.
            const auto bs = play (s, 256, 90.0, Conductor::busBass);
            auto brms = [&bs, barSec] (double b0, double b1)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (b0 * barSec * kRate), a1 = (size_t) (b1 * barSec * kRate);
                for (size_t i = a0; i < a1; ++i) sum += bs.left[i] * bs.left[i] + bs.right[i] * bs.right[i];
                return dB (std::sqrt (sum / (2.0 * (a1 - a0))));
            };
            const int bassBar = partLayer (song (s), Part::bass) * kBarsPerLayer;
            std::printf ("  bass by bar from %d:", bassBar - 5);
            for (int b = bassBar - 5; b < bassBar + 2; ++b) std::printf (" %.0f", brms (b, b + 1));
            std::printf ("   bars 8-12 %.1f vs 36-40 %.1f dB\n", brms (8, 12), brms (36, 40));

            // Brightness: the RMS of the sample-to-sample difference, which weighs
            // each octave up 6 dB more, so it follows the filter, not the level.
            auto bright = [&bs, barSec] (double b0, double b1)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (b0 * barSec * kRate), a1 = (size_t) (b1 * barSec * kRate);
                for (size_t i = a0 + 1; i < a1; ++i) { const double d = bs.left[i] - bs.left[i - 1]; sum += d * d; }
                return dB (std::sqrt (sum / (double) (a1 - a0)));
            };
            std::printf ("  bass brightness by bar from %d:", bassBar - 4);
            for (int b = bassBar - 4; b < bassBar + 2; ++b) std::printf (" %.0f", bright (b, b + 1));
            std::printf ("   bars 8-12 %.1f vs 36-40 %.1f dB\n", bright (8, 12), bright (36, 40));

            // The pad rising with the riser: a half bar at a time through the
            // sweep, then the first bar it is in for real.
            const auto pd = play (s, 256, 90.0, Conductor::busPad);
            std::printf ("  pad through the riser (dB per half bar from bar %d):", padBar - 2);
            for (double b = padBar - 2; b < padBar + 1.0; b += 0.5)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (b * barSec * kRate), a1 = (size_t) ((b + 0.5) * barSec * kRate);
                for (size_t i = a0; i < a1; ++i) sum += pd.left[i] * pd.left[i] + pd.right[i] * pd.right[i];
                std::printf (" %.0f", dB (std::sqrt (sum / (2.0 * (a1 - a0)))));
            }
            std::printf ("\n");
        }

        // Slow motion (on 40-44 s): the drum bus's brightness — RMS of the
        // first difference — before, during and after, which the master
        // lowpass should pull down hard.
        {
            const auto dm = play (s, 256, 50.0, Conductor::busDrums);
            auto bright = [&dm] (double t0, double t1)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (t0 * kRate), a1 = (size_t) (t1 * kRate);
                for (size_t i = a0 + 1; i < a1; ++i) { const double d = dm.left[i] - dm.left[i - 1]; sum += d * d; }
                return dB (std::sqrt (sum / (double) (a1 - a0)));
            };
            std::printf ("  slow motion, drum brightness: before %.1f  during %.1f  after %.1f dB\n",
                         bright (36.0, 39.5), bright (40.8, 43.8), bright (45.5, 49.0));
            std::printf ("  slow motion onset, drum brightness per 50 ms from 39.9 s:");
            for (double t = 39.9; t < 40.25; t += 0.05) std::printf (" %.0f", bright (t, t + 0.05));
            std::printf ("\n");

            const auto nz = play (s, 256, 45.0, Conductor::busSlowNoise);
            double sum = 0.0;
            for (size_t i = (size_t) (41.0 * kRate); i < (size_t) (43.8 * kRate); ++i) sum += nz.left[i] * nz.left[i] + nz.right[i] * nz.right[i];
            std::printf ("  slow motion noise alone: %.1f dB RMS\n", dB (std::sqrt (sum / (2.0 * 2.8 * kRate))));
        }

        // The arp filter opens with survival: the arp bus alone, early against late.
        {
            const auto ar = play (s, 256, 95.0, Conductor::busArp);
            auto rms = [&ar] (double t0, double t1)
            {
                double sum = 0.0;
                const size_t a0 = (size_t) (t0 * kRate), a1 = (size_t) (t1 * kRate);
                for (size_t i = a0; i < a1; ++i) sum += ar.left[i] * ar.left[i] + ar.right[i] * ar.right[i];
                return dB (std::sqrt (sum / (2.0 * (a1 - a0))));
            };
            std::printf ("  arp by survival: 0-10 s %.1f  30-40 s %.1f  80-95 s %.1f dB\n", rms (0, 10), rms (30, 40), rms (80, 95));
        }

        // Events -------------------------------------------------------------
        int shots = 0, layerUps = 0, layerDowns = 0, drops = 0, chords = 0, maxLayer = 0;
        for (const auto& e : a.events)
        {
            switch (e.type)
            {
                case Event::shot:      ++shots; break;
                case Event::layerUp:   ++layerUps; maxLayer = std::max (maxLayer, e.a); break;
                case Event::layerDown: ++layerDowns; break;
                case Event::dropEnd:   ++drops; break;
                case Event::chord:     ++chords; break;
                default: break;
            }
        }
        std::printf ("  shots %d  chord changes %d  layer ups %d (max %d)  downs %d  drop ends %d\n",
                     shots, chords, layerUps, maxLayer, layerDowns, drops);

        if (maxLayer < np::audio::maxLayer (sg)) fail ("never reached the top layer");
        if (layerDowns != 1)      fail ("death should strip exactly one layer");
        if (drops != 1)           fail ("the drop after a death never ended");
        if (shots < 200)          fail ("too few shots");

        // Block size must change nothing ---------------------------------------
        bool same = a.left.size() == b.left.size()
                 && std::memcmp (a.left.data(), b.left.data(), a.left.size() * sizeof (float)) == 0
                 && std::memcmp (a.right.data(), b.right.data(), a.right.size() * sizeof (float)) == 0;
        if (! same)
        {
            size_t first = 0;
            while (first < a.left.size() && a.left[first] == b.left[first] && a.right[first] == b.right[first]) ++first;
            std::printf ("  first differing sample: %zu (%.3f s)  L %g vs %g  R %g vs %g\n", first, first / kRate,
                         a.left[first], b.left[first], a.right[first], b.right[first]);
            fail ("256 and 128 sample blocks render differently");

            // Which bus is it?
            for (int bus = 0; bus < 6; ++bus)
            {
                const auto x = play (s, 256, 5.0, 1 << bus);
                const auto y = play (s, 128, 5.0, 1 << bus);
                size_t f = 0;
                while (f < x.left.size() && x.left[f] == y.left[f] && x.right[f] == y.right[f]) ++f;
                if (f < x.left.size())
                    std::printf ("    bus %d differs first at sample %zu: %g vs %g\n", bus, f, x.left[f], y.left[f]);
            }
        }

        bool sameEvents = a.events.size() == b.events.size();
        for (size_t i = 0; sameEvents && i < a.events.size(); ++i)
            sameEvents = a.events[i].type == b.events[i].type && a.events[i].a == b.events[i].a
                      && a.events[i].b == b.events[i].b && a.events[i].c == b.events[i].c;
        if (! sameEvents) fail ("256 and 128 sample blocks produce different events");
        else              std::printf ("  block size: identical samples and %zu identical events\n", a.events.size());

        // Each bus alone, over the full-layer stretch before the death (80-95 s).
        static const char* busNames[6] { "drums", "bass", "arp", "hits", "pad", "wet" };
        std::printf ("  buses, 80-95 s:");
        for (int bus = 0; bus < 6; ++bus)
        {
            const auto solo = play (s, 256, 95.0, 1 << bus);
            double sum = 0.0, pk = 0.0;
            const size_t from = (size_t) (80.0 * kRate), to = solo.left.size();
            for (size_t i = from; i < to; ++i)
            {
                sum += solo.left[i] * solo.left[i] + solo.right[i] * solo.right[i];
                pk = std::max (pk, (double) std::max (std::abs (solo.left[i]), std::abs (solo.right[i])));
            }
            std::printf (" %s %.1f/%.1f", busNames[bus], dB (std::sqrt (sum / (2.0 * (to - from)))), dB (pk));
        }
        std::printf ("  (RMS/peak dB)\n");

        writeWav (outDir + "/neonpuls-" + std::to_string (s + 1) + ".wav", a);
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
