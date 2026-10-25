// Renders each song with every layer in, for listening: the stick goes round
// the wheel a chord a bar, so the strum (on chord changes only) plays every
// bar. Two files per song: the whole band, and the drone, strum and pad alone.
//
//   build.bat layerpreview [outdir]

#include "../src/audio/Conductor.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace np::audio;

namespace
{
    constexpr int kRate = 48000;

    void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
    {
        FILE* f = std::fopen (path.c_str(), "wb");
        if (! f) { std::printf ("cannot write %s\n", path.c_str()); return; }
        const std::uint32_t frames = (std::uint32_t) l.size(), bytes = frames * 4;
        auto u32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (kRate); u32 (kRate * 4); u16 (4); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (bytes);
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            const auto a = (std::int16_t) std::lround (std::clamp (l[i], -1.0f, 1.0f) * 32767.0f);
            const auto b = (std::int16_t) std::lround (std::clamp (r[i], -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
        }
        std::fclose (f);
    }

    double lastRms = 0.0;

    void render (int songIndex, int busMask, bool fire, float gain, const std::string& path, int strumStyle = 0)
    {
        const Song& s = song (songIndex);
        auto band = std::make_unique<Conductor>();
        band->prepare (kRate);
        band->busMask = busMask;
        band->strumStyle = strumStyle;
        band->endless = true;
        band->requestedChord = s.homeSlot;
        band->post ({ Command::start, songIndex, 0 });
        band->post ({ Command::setLayer, maxLayer (s) });
        band->firing = fire;

        const double samplesPerBeat = kRate * 60.0 / s.bpm;
        const double samplesPerBar = samplesPerBeat * 4.0;
        const int bars = 16;
        const size_t total = (size_t) (samplesPerBar * bars);
        std::vector<float> outL (total + (size_t) kRate * 4), outR (total + (size_t) kRate * 4);

        constexpr int block = 256;
        for (size_t pos = 0; pos < outL.size(); pos += block)
        {
            // The next bar's chord half a beat before it.
            const int chordBar = (int) (((double) pos + samplesPerBeat * 0.5) / samplesPerBar);
            band->requestedChord = (s.homeSlot + chordBar) % 4;
            if (pos >= total) { band->firing = false; band->post ({ Command::stop }); }

            const int n = (int) std::min<size_t> (block, outL.size() - pos);
            band->render (outL.data() + pos, outR.data() + pos, n);
            Event e;
            while (band->poll (e)) {}
        }

        double sum = 0.0;
        for (size_t i = (size_t) (samplesPerBar * 2); i < total; ++i) sum += outL[i] * outL[i] + outR[i] * outR[i];
        lastRms = 20.0 * std::log10 (std::sqrt (sum / (2.0 * (double) (total - (size_t) (samplesPerBar * 2)))) + 1e-12);
        if (path.empty()) return;

        for (auto& x : outL) x *= gain;
        for (auto& x : outR) x *= gain;
        writeWav (path, outL, outR);
        std::printf ("wrote %s\n", path.c_str());
    }
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    // "styles": the strum's chord choices (Conductor::strumStyle) against the
    // stick's chord on the pad and drone, for choosing by ear.
    if (argc > 2 && std::string (argv[2]) == "styles")
    {
        const char* songNames[3] { "pulse", "acid", "starfall" };
        const char* styles[5] { "1-same-chord", "2-third-up", "3-fifth-up", "4-add9", "5-fourths" };
        for (int i = 0; i < 3; ++i)
            for (int st = 0; st < 5; ++st)
                render (i, Conductor::busDrone | Conductor::busStrum | Conductor::busPad | Conductor::busWet, false, 1.1f,
                        dir + "/" + songNames[i] + "-strum-" + styles[st] + ".wav", st);
        return 0;
    }

    if (argc > 2 && std::string (argv[2]) == "measure")
    {
        for (int i = 0; i < 3; ++i)
        {
            std::printf ("%s:", song (i).name);
            for (int bus : { (int) Conductor::busPad, (int) Conductor::busDrone, (int) Conductor::busStrum, (int) Conductor::busWet, (int) Conductor::busAll })
            {
                render (i, bus, false, 1.0f, "");
                std::printf (" %.1f", lastRms);
            }
            std::printf ("  (pad, drone, strum, wet, all dB RMS)\n");
        }
        return 0;
    }
    const char* names[3] { "pulse-harp", "acid-vibes", "starfall-guitar" };
    for (int i = 0; i < 3; ++i)
    {
        render (i, Conductor::busAll, true, 1.0f, dir + "/" + names[i] + "-full-band.wav");
        // Alone they are quiet beside a full mix; lifted so they can be heard.
        render (i, Conductor::busDrone | Conductor::busStrum | Conductor::busPad | Conductor::busWet, false, 1.3f,
                dir + "/" + names[i] + "-drone-strum-pad.wav");
    }
    return 0;
}
