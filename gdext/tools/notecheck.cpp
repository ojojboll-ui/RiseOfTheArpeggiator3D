// Prints the strum's notes for each style on PULSE's verse wheel: a check that
// every style stays in the key. Not part of the build.
#include "../src/audio/Conductor.h"
#include <cstdio>
#include <memory>
using namespace np::audio;
int main()
{
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int st = 0; st < 5; ++st)
    {
        auto c = std::make_unique<Conductor>();
        c->prepare (48000); c->strumStyle = st; c->endless = true;
        c->post ({ Command::start, 0, 0 }); c->post ({ Command::setLayer, 8 });
        std::printf ("style %d:", st);
        std::vector<float> l (256), r (256);
        int lastChord = -1;
        for (int b = 0; b < 48000 * 12 / 256; ++b)
        {
            c->requestedChord = (b * 256 / 48000 / 2) % 4;
            c->render (l.data(), r.data(), 256);
            Event e; while (c->poll (e)) {}
            if (c->pubChord.load() != lastChord && c->debugStrumReady())
            {
                lastChord = c->pubChord.load();
                std::printf ("  [");
                for (int n : c->debugStrumNotes()) std::printf (" %s", names[n % 12]);
                std::printf (" ]");
            }
        }
        std::printf ("\n");
    }
}
