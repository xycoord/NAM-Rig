#pragma once

#include <vector>

#include "AdtPrelude.h"
#include "ModelSlot.h"
#include "dsp/RecursiveLinearFilter.h"

namespace namrig::engine
{

// The amp stage of the v1 chain, now up to two lanes (dual-mono stereo).
// Gains and the IR stage live in the JUCE processor.
//
//   [input gain] -> NAM model xN -> DC blocker -> [IR] -> [output gain]
//
// No JUCE anywhere in this layer. prepare() off the audio thread only;
// process() on it, allocation-free (buffers preallocated, oversized blocks
// chunked, DC blocker always run at a constant 2-channel shape so a width
// change never reallocates its history).
class Engine
{
public:
    // Audio stopped. Sizes every buffer for maxBlockSize; process() chunks
    // anything larger a host might still hand us.
    void prepare(double sampleRate, int maxBlockSize);

    // In-place on 1 or 2 mono lane buffers. Audio thread.
    // With a stereo model pair, lanes process independently; during the
    // transient where the pair's width lags the requested width, lane 0's
    // model is used and its output copied (never shared state across lanes).
    void process(float* const* lanes, int numLanes, int numFrames);

    ModelSlot& models() { return modelSlot; }
    const ModelSlot& models() const { return modelSlot; }

    int latencySamples() const { return modelSlot.latencySamples(); }

private:
    void processChunk(float* const* lanes, int numLanes, int numFrames);

    ModelSlot modelSlot;
    recursive_linear_filter::HighPass dcBlocker;
    std::vector<float> scratch0, scratch1; // model outputs per lane

    double sampleRate = 48000.0;
    int maxBlock = 0;

    static constexpr double kDcBlockerHz = 5.0;
};

} // namespace namrig::engine
