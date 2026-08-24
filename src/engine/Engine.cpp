#include "Engine.h"

#include <algorithm>
#include <cstring>

namespace namrig::engine
{

void Engine::prepare(const double newSampleRate, const int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;

    scratch0.assign(static_cast<size_t>(maxBlock), 0.0f);
    scratch1.assign(static_cast<size_t>(maxBlock), 0.0f);

    dcBlocker.SetParams({sampleRate, kDcBlockerHz});

    // Push one max-size silent 2-channel block through the filter so its
    // internal buffers reach full capacity now; the audio thread always
    // processes it 2-channel, so it never sees a shape change.
    float* ptrs[2] = {scratch0.data(), scratch1.data()};
    (void)dcBlocker.Process(ptrs, 2, maxBlock);
    std::fill(scratch0.begin(), scratch0.end(), 0.0f);
    std::fill(scratch1.begin(), scratch1.end(), 0.0f);

    modelSlot.prepare(sampleRate, maxBlockSize);
    pitchTuner.prepare(sampleRate);
}

void Engine::process(float* const* lanes, const int numLanes, const int numFrames)
{
    // Hosts can exceed the promised block size (offline render, freeze).
    // Rule 1: chunk, never throw.
    int offset = 0;
    while (offset < numFrames)
    {
        const int n = std::min(numFrames - offset, maxBlock);
        float* chunk[2] = {lanes[0] + offset,
                           numLanes > 1 ? lanes[1] + offset : nullptr};
        processChunk(chunk, numLanes, n);
        offset += n;
    }
}

void Engine::processChunk(float* const* lanes, const int numLanes, const int numFrames)
{
    const size_t bytes = sizeof(float) * static_cast<size_t>(numFrames);

    // Tuner tap: post-trim, pre-amp — the clean staged signal. Lane 0 is
    // representative (stereo lanes carry the same instrument).
    pitchTuner.push(lanes[0], numFrames);

    float* scratch[2] = {scratch0.data(), scratch1.data()};

    if (ModelPair* pair = modelSlot.render())
    {
        for (int l = 0; l < numLanes; ++l)
        {
            if (l < pair->numLanes)
            {
                float* in[1] = {lanes[l]};
                float* out[1] = {scratch[l]};
                pair->lane[static_cast<size_t>(l)]->process(in, out, numFrames);
            }
            else
            {
                // Width transient (pair narrower than requested): reuse
                // lane 0's OUTPUT — never its stateful model.
                std::memcpy(scratch[l], scratch[0], bytes);
            }
        }
    }
    else
    {
        for (int l = 0; l < numLanes; ++l)
            std::memcpy(scratch[l], lanes[l], bytes);
    }

    // Unused lane stays silent so the DC blocker's constant 2-channel shape
    // sees defined data.
    if (numLanes < 2)
        std::memset(scratch[1], 0, bytes);

    float** dcOut = dcBlocker.Process(scratch, 2, numFrames);
    for (int l = 0; l < numLanes; ++l)
        std::memcpy(lanes[l], dcOut[l], bytes);
}

} // namespace namrig::engine
