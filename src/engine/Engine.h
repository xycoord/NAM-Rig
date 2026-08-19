#pragma once

#include <vector>

#include "AdtPrelude.h"
#include "ModelSlot.h"
#include "dsp/RecursiveLinearFilter.h"

namespace namrig::engine
{

// The v1 chain, mono, minus the gains (which live in the JUCE processor so
// they can be juce::SmoothedValues):
//
//   [input gain] -> NAM model -> (IR: milestone 2b) -> DC blocker -> [output gain]
//
// No JUCE anywhere in this layer. prepare() off the audio thread only;
// process() on it, allocation-free (buffers preallocated, oversized blocks
// chunked).
class Engine
{
public:
    // Audio stopped. Sizes every buffer for maxBlockSize; process() chunks
    // anything larger a host might still hand us.
    void prepare(double sampleRate, int maxBlockSize);

    // In-place on a mono buffer. Audio thread.
    void process(float* samples, int numFrames);

    ModelSlot& models() { return modelSlot; }
    const ModelSlot& models() const { return modelSlot; }

    int latencySamples() const { return modelSlot.latencySamples(); }

private:
    void processChunk(float* samples, int numFrames);

    ModelSlot modelSlot;
    recursive_linear_filter::HighPass dcBlocker;
    std::vector<float> scratch; // model output, sized maxBlock

    double sampleRate = 48000.0;
    int maxBlock = 0;

    static constexpr double kDcBlockerHz = 5.0;
};

} // namespace namrig::engine
