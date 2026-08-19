#pragma once

// The vendored AudioDSPTools headers carry two stray iPlug2-isms that only
// compiled because the old plugin was built inside iPlug2. Provide the two
// missing pieces instead of patching the submodule. Include this BEFORE any
// AudioDSPTools header.
//
// (Its ImpulseResponse also hardcodes double and so can't build with
// DSP_SAMPLE=float; we don't compile it — IR convolution will use
// juce::dsp::Convolution, see docs/plan.md watchlist.)

#ifndef DEFAULT_BLOCK_SIZE
    #define DEFAULT_BLOCK_SIZE 512 // ResamplingContainer::Reset default arg only
#endif

namespace iplug
{
inline constexpr double PI = 3.14159265358979323846; // LanczosResampler kernel
}
