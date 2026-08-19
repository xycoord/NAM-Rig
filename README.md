# NAM Rig

A ground-up rebuild of the [Neural Amp Modeler plugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin) on JUCE 8.
Standalone practice rig first, then CLAP + VST3. Linux, Windows, macOS from one codebase.

## Building (Linux)

```bash
sudo apt install libasound2-dev libfreetype-dev libfontconfig-dev \
  libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libxcomposite-dev libcurl4-openssl-dev ninja-build

git clone --recurse-submodules --shallow-submodules <repo-url>
cd nam-rig
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/NamRig_artefacts/Release/Standalone/"NAM Rig"
```

## Status

Milestone 1: standalone shell with smoothed passthrough gain.
The NAM engine arrives in milestone 2.
