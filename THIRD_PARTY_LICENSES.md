# Third-party licenses

NAM Rig is licensed under the GNU Affero General Public License v3.0
(see `LICENSE`). It incorporates the following third-party software and
assets. Full license texts live alongside each dependency in `libs/` and
`assets/`; this file is the index, and it ships with binary releases so
the notices travel with the binaries as their licenses require.

## Frameworks and libraries

| Component | License | Source |
|---|---|---|
| JUCE 8 | AGPLv3 (of its AGPLv3/commercial dual license) | https://github.com/juce-framework/JUCE — `libs/JUCE/LICENSE.md` |
| NeuralAmpModelerCore | MIT — Copyright (c) 2023 Steven Atkinson | https://github.com/sdatkinson/NeuralAmpModelerCore — `libs/NeuralAmpModelerCore/LICENSE` |
| AudioDSPTools | MIT — Copyright (c) 2023 Steven Atkinson | https://github.com/sdatkinson/AudioDSPTools — `libs/AudioDSPTools/LICENSE` |
| clap-juce-extensions | MIT — Copyright 2019-2020, Paul Walker | https://github.com/free-audio/clap-juce-extensions — `libs/clap-juce-extensions/LICENSE.md` |
| Eigen | MPL2 (built with `EIGEN_MPL2_ONLY`; no LGPL components) | https://eigen.tuxfamily.org — `libs/eigen/COPYING.MPL2` |
| nlohmann/json | MIT — Copyright (c) 2013-2022 Niels Lohmann | vendored in NeuralAmpModelerCore (`libs/NeuralAmpModelerCore/Dependencies/nlohmann`) |

JUCE itself bundles further dependencies (FLAC, Ogg Vorbis, zlib,
HarfBuzz, and others); see `libs/JUCE/LICENSE.md` for that list.

Catch2 (BSL-1.0) is used for tests only and is not part of any
distributed binary.

## Assets

| Asset | License | Source |
|---|---|---|
| Barlow font (Regular/Medium/SemiBold) | SIL Open Font License 1.1 — Copyright 2017 The Barlow Project Authors | https://github.com/jpt/barlow — `assets/fonts/OFL.txt` |

## MIT license text (NeuralAmpModelerCore, AudioDSPTools, clap-juce-extensions, nlohmann/json)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
