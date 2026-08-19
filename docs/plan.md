# NAM Rig — plan of record

Distilled from the audit of the old plugin and the planning discussion
(August 2026). CLAUDE.md carries the day-to-day rules; this file carries the
roadmap and the reasoning, so decisions don't get relitigated by accident.

## Why a rebuild

The original iPlug2 plugin (see `~/Code/NeuralAmpModelerPlugin`) is small and
honest but shaped by one assumption — one model, one IR, six knobs, fixed
600×400 UI — and that assumption is baked into its parameter enum (which is
simultaneously GUI grid position, serialization order, and unserialization
index), its 200-line layout lambda, and its audio thread (which destroys
models, throws exceptions, allocates, and calls the host from the callback).
Its iPlug2 fork has no Linux support at all (11-line IGraphicsLinux.cpp,
commented-out main()). The DSP libraries underneath are clean and portable;
everything around them was cheaper to rebuild than retrofit.

## Settled decisions

- **JUCE 8** (pinned 8.0.15), CMake + Ninja. CLAP exported via
  clap-juce-extensions in milestone 5; VST3 likewise. No AAX/AU/AUv3/iOS/WAM.
- **Native vector UI**, FlexBox/Grid, software renderer. WebView rejected:
  six classes of Linux-only runtime failure (X11-only child windows, SIGPIPE
  DAW crashes, WebKitGTK fragmentation, GPU blank screens, noexec /tmp,
  SONAME drift).
- **Wayland**: all Linux plugin UIs are X11/XWayland today (VST3 and CLAP
  embedding are X11-only). Accepted; not a framework differentiator.
- **New plugin identity, no legacy state migration.** Old DAW projects keep
  loading the old plugin.
- **Porting behaviors, not designs.** The old UI's overlays and hidden icons
  carry no weight.
- **JACK-first audio on Linux** via pipewire-jack (~3–4 ms RTT at 64/48k vs
  ~8+ ms through the ALSA bridge). 48 kHz always: PipeWire graph is
  rate-locked to 48k and NAM models are 48k-native.

## v1 scope

Chain: `input gain → NAM model → IR → DC blocker → output gain` (all smoothed).

Keep: .nam loader + IR loader with folder-stepping and clear errors; Slim as
a first-class visible quality/CPU control (disabled with hint when the model
isn't slimmable, saved per preset); Raw/Normalized output mode; peak meters
with clip indication; auto-resampling with deferred latency reporting; named
presets (model + IR + params = one rig); full standalone persistence.

Cut from v1 (all re-addable — name-keyed state means no format break):
noise gate, tone stack, calibration system (dBu/Calibrated mode),
directory-style models, web-link buttons.

v2 horizon, rough order: reverb, tuner, backing-track player (standalone),
gate + EQ return, gain-compensation policy, metronome.

Field note (M2): long reverb IRs already work through the IR slot —
juce::dsp::Convolution is non-uniform partitioned. The v2 "reverb" may
simply be a SECOND IR slot chained after the cab slot (real spaces, no
algorithmic reverb to write). Observed cost: ~25% of a 64-sample block for
a multi-second IR; watch for partition-boundary xrun spikes at quantum 64.

## Milestones

1. **Bootstrap** — DONE (commit a073e4c). Standalone shell, JACK at 64/48k,
   native title bar, input unmuted, smoothed gain passthrough, CI, tests.
2. **Engine** — vendor NAM core + AudioDSPTools into CMake; port
   ResamplingNAM; stage chain with background loader, atomic swap, collector
   thread; Slim through the safe handoff; Raw/Normalized from model metadata;
   offline render tests (level, latency, no-model passthrough, state
   round-trip). Hardcoded paths; no new UI. Exit: an evening of practice,
   model switches never click or drop out.
3. **State** — versioned name-keyed serialization, portable paths with
   search-on-miss, standalone autosave/restore, named presets. Exit: quit and
   relaunch restores the rig exactly; presets survive a moved model folder.
4. **UI** — real editor: file-steppers, gains, Slim, output mode, meters,
   preset bar, resizable FlexBox layout, error surfaces. Replace JUCE's
   audio-settings dialog with our own devices panel: friendly channel names,
   live input level meter, in-app latency (quantum) preference. Resolve the
   scaling question (proportional vs fixed-size + space; user leans fixed).
   Exit: daily practice happens here and the old plugin isn't missed.
5. **Formats** — CLAP + VST3 targets, deferred latency reporting, DAW state
   save/restore, automation sanity; Windows + macOS join CI. Test in Reaper.
6. **Ship** — tag v1.0, install paths, README, CI artifacts for 3 platforms.

## Watchlist

- clap-juce-extensions × JUCE version pairing: pin known-good, don't chase.
- juce::dsp::Convolution latency behavior with long IRs — verify in M2
  before committing to it over AudioDSPTools' engine.
- CPU headroom at quantum 64 once WaveNet inference lands (M2): budget is
  1.3 ms/block. "Solid at 64, comfortable at 128" is the target.
- XWayland fractional-scaling softness — cosmetic, monitor-dependent.
- NAM core API drift (pre-1.0, Slim API is recent): pinned submodule,
  deliberate bumps only.
