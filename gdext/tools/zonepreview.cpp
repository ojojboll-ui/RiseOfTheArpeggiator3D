// Renders each song through its three sections — verse, chorus, bridge, and
// back to the verse — with the whole band playing, in both ways of making the
// chorus and bridge (Songs.h, SectionStyle), so they can be chosen by ear.
//
//   build.bat zonepreview [outdir]
//
// The stick goes round the wheel clockwise, a chord a bar, from the song's
// home slot; the arpeggio plays throughout; every layer is in. Eight bars of
// verse, eight of chorus, eight of bridge, four of verse.

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

    int sectionAt (int bar)
    {
        if (bar < 8) return 0;
        if (bar < 16) return 1;
        if (bar < 24) return 2;
        return 0;
    }

    void render (int songIndex, SectionStyle style, const std::string& path)
    {
        const Song& s = song (songIndex);
        auto band = std::make_unique<Conductor>();
        band->prepare (kRate);
        band->styleOverride = (int) style;
        band->endless = true;
        band->requestedSection = 0;
        band->requestedChord = s.homeSlot;
        band->post ({ Command::start, songIndex, 0 });
        band->post ({ Command::setLayer, maxLayer (s) });
        band->firing = true;

        const double samplesPerBeat = kRate * 60.0 / s.bpm;
        const double samplesPerBar = samplesPerBeat * 4.0;
        const int bars = 28;
        const size_t total = (size_t) (samplesPerBar * bars);
        std::vector<float> outL (total + (size_t) kRate * 3), outR (total + (size_t) kRate * 3);

        constexpr int block = 256;
        for (size_t pos = 0; pos < outL.size(); pos += block)
        {
            // The next bar's chord half a beat before it, its section a beat
            // and a half before it (so the roll on beat four is asked for).
            const double t = (double) pos;
            const int chordBar = (int) ((t + samplesPerBeat * 0.5) / samplesPerBar);
            const int sectionBar = (int) ((t + samplesPerBeat * 1.5) / samplesPerBar);
            band->requestedChord = (s.homeSlot + chordBar) % 4;
            band->requestedSection = sectionAt (std::min (sectionBar, bars - 1));
            if (pos >= total) { band->firing = false; band->post ({ Command::stop }); }

            const int n = (int) std::min<size_t> (block, outL.size() - pos);
            band->render (outL.data() + pos, outR.data() + pos, n);

            Event e;
            while (band->poll (e)) {}
        }

        writeWav (path, outL, outR);

        // What was played, for the listener.
        std::printf ("wrote %s\n", path.c_str());
        for (int sec = 0; sec < kSections; ++sec)
        {
            const Wheel w = sectionWheel (s, sec, style);
            std::printf ("   %-7s", sectionName (sec));
            for (int k = 0; k < 4; ++k)
            {
                char name[8];
                chordName (s, w, (s.homeSlot + k) % 4, name, sizeof (name));
                std::printf (" %-4s", name);
            }
            std::printf ("\n");
        }
    }
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const char* names[3] { "pulse", "acid", "starfall" };
    for (int i = 0; i < 3; ++i)
    {
        render (i, SectionStyle::keyLift,   dir + "/" + names[i] + "-A-key-lift.wav");
        render (i, SectionStyle::newChords, dir + "/" + names[i] + "-B-new-chords.wav");
    }
    return 0;
}
