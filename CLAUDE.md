# NAM Rig

Ground-up rebuild of the Neural Amp Modeler plugin on JUCE 8. Standalone
guitar practice rig first, then CLAP + VST3. The user's old fork at
`~/Code/NeuralAmpModelerPlugin` is the reference implementation — read it to
answer "what did the original do?", never copy its structure.

Roadmap, v1 scope, and full architecture rationale: `docs/plan.md`.

## Build & run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure (once)
cmake --build build                                   # build
ctest --test-dir build --output-on-failure            # tests (Catch2)
./build/NamRig_artefacts/Release/Standalone/"NAM Rig" # run standalone
```

Submodules are shallow; after a fresh clone: `git submodule update --init --depth 1`.
JUCE is pinned to the 8.0.15 tag — do not bump to JUCE 9 without discussion
(clap-juce-extensions compatibility).

## Architecture rules (violating one of these is a bug by definition)

1. The audio thread never allocates, frees, locks, throws, or calls the host.
   Buffers are sized in `prepareToPlay`; oversized blocks are chunked, not thrown at.
2. Models/IRs swap by atomic handoff: load on a background thread, publish via
   lock-free exchange, retire old objects on a collector thread. Slim changes
   ride the same mechanism.
3. Latency changes: flag from the audio thread, apply from the message thread.
4. Every gain is a `juce::SmoothedValue`. No raw scalars the message thread can step.
5. Parameters have stable string IDs (`src/state/Parameters.h`); saved state is
   versioned and name-keyed. Never reuse or reorder a parameter ID.
6. Paths are stored portably (model root + relative path); absolute path is a
   fallback hint only.
7. `src/engine/` never includes JUCE GUI headers; the UI never touches DSP
   objects. Communication: parameters, state, lock-free FIFOs.
8. UI stays cheap: software renderer (no OpenGL context), vectors not bitmaps,
   partial repaints, ~30 Hz meter timer stopped when the editor closes.

Signal chain (all gains smoothed, per-section bypass crossfaded):
trim → staging meter tap → [AMP: HPF → LPF → drive → model ×lanes,
measured-compensated] → DC blocker → [CAB IR: 1/2/4ch topologies] →
[REVERB: parallel send → send HPF/LPF → pre-delay → convolution] →
normalization (always on, kNormTargetDb) → out. Channels Auto/Mono/Stereo
= dual-mono ModelPair lanes. Tuner (engine/Tuner.cpp) taps post-trim on
its own worker. Drive compensation comes from a load-time measured rise
curve (ModelSlot::measureRiseCurve). There is NO user output gain: staged
input + measured drive + normalization ⇒ output ≈ bypass by construction.

## Platform notes (user's machine: Ubuntu, Wayland, PipeWire, Scarlett 2i4)

- Audio runs as a **JACK client via pipewire-jack** (`JUCE_JACK=1`); the shell
  defaults to JACK and requests quantum 64/48000 via env at startup
  (`src/standalone/StandaloneApp.cpp`). Keep the rig at 48 kHz — the PipeWire
  graph is rate-locked to 48k and NAM models are 48k-native.
- **Never `pkill` the app to restart it** — JUCE saves settings only on clean
  quit; killing it silently discards the user's device setup. Ask the user to
  close the window. Also `pkill -f` matches your own shell; use `pkill -x "NAM Rig"`
  only when state loss is acceptable.
- Under JACK, sample rate / buffer size are graph-owned and read-only in the
  device dialog. Change via `PIPEWIRE_QUANTUM` env or `pw-metadata`.
- Inspect the graph with `pw-top -b -n 2` and `pw-dump` (look at `client.api`
  to confirm jack vs alsa routing).
- No `gh` CLI and no non-interactive sudo: hand `sudo apt …` commands to the
  user; GitHub access is SSH push/pull only (`git@github.com:xycoord/NAM-Rig.git`).

## UI system

`ui/Theme.h`: color tokens + custom LookAndFeel (embedded Barlow via
BinaryData; flat knobs, constant 5px arcs, in-knob values). Rules: accent
only on interactive value (LPF-style knobs set "reverseFill" — arc shows
how much the control is DOING); green only meter-zone/in-tune; red only
clip/error; readouts (tuner, meters) float frameless, controls live in
titled panels; per-IR topology shows in section headers; a healthy system
is silent — status text only when the user should act. Controls follow
one-question-one-control: Drive is the only large knob, Trim is a fader,
Quality is a dropdown of the model's real breakpoints. KnobSlider: drag /
shift-fine / double-click-default / click-value-to-type.

## Style

- C++20, 4-space indent, Allman braces, ~100 cols (`.clang-format` is canonical).
- `namespace namrig`; members `camelCase` (no `m` prefix); JUCE idioms over stdlib
  where JUCE has the tool (`juce::String` in UI code, `std::` in `src/engine/`).
- Comments explain *why*, and every deliberate deviation from the old plugin's
  behavior gets one.
- Tests for engine and state code are not optional; UI code is exempt.

## The user

Guitarist on Linux; uses Reaper; wants "plug in and play" with minimal fuss.
Prefers clean flat UIs, low CPU, and honest latency numbers. Comfortable with
git and the terminal; hand them one-liners rather than walls of steps.
