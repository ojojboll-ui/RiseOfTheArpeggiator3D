# Rise of the Arpeggiator 3D

The neon twin-stick shooter where the music *is* the game, rebuilt in 3D with
lowpoly models on a floor that ripples. The left stick flies the ship and picks
one of four chords; the right stick aims and plays the arpeggio — every note of
the arpeggio is a shot. Enemies die in chords and notes, and the track builds
from a lone kick drum into the full song for as long as you stay alive.

This is the 3D successor of
[Rise of the Arpeggiator](https://github.com/ojojboll-ui/RisOfTheArpeggiator).
The band and the rules are the same C++ code, running inside Godot as a
GDExtension; Godot draws the world.

**A level** is three rooms joined by narrower passages, each a section of the
song with its own four chords: the **verse** (hills and valleys), the
**chorus** (walls) and the **bridge** (both). Flying into a room changes the
section on the next bar. Each room has a tower in its colour; when all three
are down, three stronger ones rise, one in each room again. The way out is a
corridor at the top shut by six doors, two in each room's colour, and every
tower that falls takes away one of them. Through the last door is the exit:
fly into it for the next song — PULSE, then ACID, then STARFALL — with your
score, lives and bombs.

The ship is a hovercraft: it carries its momentum and slides through turns,
and hills slow it going up and speed it going down — enemies too. Shooting
down a black hole (the orange ball) clears every enemy on the screen, as the
bomb does. Power-ups are green balls inside two spinning rings.

Slow motion leaves trails behind everything that shines.

Still to come: a pass on looks and menus.

## Controls

Any gamepad Godot knows (Xbox, PlayStation, Switch). Keyboard and mouse work too.

| | Gamepad | Keyboard and mouse |
|---|---|---|
| Fly, and pick the chord | Left stick | W A S D |
| Aim and fire (plays the arpeggio) | Right stick | Arrow keys, or hold the left mouse button |
| Bomb | RT, LB or RB | Space, or right mouse button |
| Slow motion (hold) | LT | Shift |
| Pause | Start | Esc or P |
| Confirm | A | Enter |
| Back | B | Backspace |

In the menu: left and right choose the song, up and down the difficulty.

## Building and running

Windows, with:

- Godot 4.5 or newer (developed on 4.7.2)
- Visual Studio 2022 or its Build Tools, with the C++ workload
- CMake 3.17 or newer, Python 3, Git

```bat
git clone --recursive https://github.com/ojojboll-ui/RiseOfTheArpeggiator3D
cd RiseOfTheArpeggiator3D\gdext
build.bat
```

That builds godot-cpp and the game's extension into `bin\` (close Godot
first, it locks the dll). Then open the folder in Godot and press F5, or run
`run.bat` with `GODOT` set to your Godot executable.

## Tests

The band is plain C++ and is tested without Godot, as in the 2D game:

```bat
cd gdext\tools
build.bat audiotest ..\..\out
```

`audiotest` plays every song with scripted input at two buffer sizes and
requires bit-identical output, checks layers, deaths, drops, slow motion,
build-ups and section changes, and writes a WAV per song. `build.bat
zonepreview ..\..\out` renders each song through its sections.

The game takes flags after `--`: `--demo` (the autopilot plays), `--level N`,
`--difficulty easy|normal|hard`, `--seconds T`, `--shots DIR --every S`
(screenshots), `--wav PATH`, `--reckless`, `--slowtest`, `--counts`, `--lookexit`, `--pickup`. `snap.sh` wraps them.

## Layout

| | |
|---|---|
| `gdext/src/audio/` | The band: conductor, songs, synths, drums (from the 2D game) |
| `gdext/src/game/` | The rules and the autopilot; `Level` is the rooms, walls, hills and way-finding |
| `gdext/src/arp_game.*` | The Godot node: steps the world, hands Godot what to draw |
| `gdext/src/band_stream.*` | The band as an AudioStream on Godot's mixing thread |
| `scripts/` | Scene, camera, lowpoly models, HUD and menu, input |
| `shaders/` | Neon outlines, the rippling floor, sparks, rings, slow-motion blur |
