# Particle Audio Engine

A small C++17 graphics engine built on [raylib](https://www.raylib.com/) that
drives real-time particle physics simulations from live audio analysis —
an FFT-based music visualizer where the particles are a physics sim, not
just a spectrum plot.

## How it works

- **`audio/AudioAnalyzer`** taps the raylib audio stream (via
  `AttachAudioStreamProcessor`), runs a custom radix-2 FFT
  (`audio/FFT`) each frame on a Hann-windowed sample buffer, and derives:
  - a smoothed frequency spectrum and 48 log-spaced display bars
  - bass / mid / treble band energies
  - simple energy-based beat detection (with cooldown)
- **`particles/ParticleSystem`** is a fixed-capacity particle pool (free-list
  based, O(1) emit/kill) driven by a stack of composable `IForce`s:
  gravity wells, drag, vortices, curl-noise turbulence, and constant
  directional forces (`particles/Forces`).
- **`visualizers/`** contains five independent scenes that wire audio
  features to particle emission and force parameters:
  1. **Neon Fog** — a dense, lit-from-within fog volume, a fraction of
     which is attracted onto an SDF shape field (`shapes/ShapeField` +
     `ProceduralShapeProvider`) via a `ShapeConform` force, cycling
     through analytic primitives (sphere/box/torus/cylinder) so
     particles visibly migrate to each new shape. Shape attraction is
     driven by an independent `morphForce_` value (`-`/`=`), *not*
     audio; audio only drives the core light's color/intensity and
     triggers lightning. See the class comment in
     `visualizers/NeonFogVisualizer.h` for the full design rationale,
     including the extension point for a future mesh-driven shape
     (`assets/models/` has test meshes but nothing loads them yet).
  2. **Spectrum Ring** — a circular bar-graph EQ that shoots sparks off
     each bar, with gravity/drag on the sparks and an expanding pulse
     ring on every beat.
  3. **Particle Galaxy** — a central gravity well with continuously
     spawned particles kicked into orbit; bass strengthens gravity,
     treble adds turbulence, beats punch particles outward.
  4. **Fireworks** — beats launch shells that arc and explode into
     radial bursts under gravity + drag.
  5. **Audio Tunnel** — a particle tunnel flying toward the camera,
     radius modulated per-angle by the spectrum, color cycling and
     flashing on beats.
- **`app/App`** owns the window, camera (orbit/zoom via mouse), audio
  playback, and switches between visualizers.

No external assets are required: particle sprites are a procedurally
generated radial-gradient texture, and if `assets/audio/` is empty the
engine synthesizes a short procedural track (kick/bass/arpeggio/hi-hats)
so there's always something rhythmic to react to.

## Building

Requires CMake 3.20+ and a C++17 compiler. raylib is fetched
automatically via `FetchContent` (needs internet access on first
configure).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows with Visual Studio instead of Ninja:

```sh
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The built binary copies `assets/` next to itself automatically.

## Running

```sh
# Use your own track
./build/ParticleAudioEngine path/to/song.mp3

# Or just drop a file into assets/audio/ and run with no args
./build/ParticleAudioEngine
```

## Controls

| Key | Action |
|---|---|
| `1`-`5` / `Tab` / `→` / `←` | Jump to / cycle visualizers (wired for the other four; only Neon Fog is currently registered — see `App::Init`) |
| `Space` | Pause/resume |
| `S` | Cycle shape preset (Neon Fog) |
| `M` | Toggle shape auto-cycle on/off (Neon Fog) |
| `-` / `=` | Decrease / increase shape attraction force (Neon Fog) |
| Right-drag mouse | Orbit camera |
| Mouse wheel | Zoom |
| `C` | Toggle camera auto-rotate |
| `R` | Reset camera |
| `[` / `]` | Decrease / increase reactivity intensity (lighting only) |
| `F` | Toggle fullscreen |
| `H` | Toggle the plain-text HUD (FPS/particle count) |
| `F1` | Toggle the ImGui settings panel |
| `F12` | Save a screenshot |
| `Esc` | Quit |

The bottom of the window also has an always-on playback transport (prev/
play-pause/next, a scrubbable timeline, volume) independent of the HUD/panel
above.

## Extending it

- New visualizer: implement `Visualizer` (see `visualizers/Visualizer.h`),
  register it in `App::Init`.
- New force: implement `IForce` (see `particles/Forces.h`).
- Tune beat sensitivity / band ranges in `audio/AudioAnalyzer.cpp`.
