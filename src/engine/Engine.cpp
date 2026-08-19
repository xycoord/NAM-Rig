#include "Engine.h"

#include <algorithm>
#include <cstring>

namespace namrig::engine
{

void Engine::prepare(const double newSampleRate, const int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;

    scratch.assign(static_cast<size_t>(maxBlock), 0.0f);

    dcBlocker.SetParams({sampleRate, kDcBlockerHz});

    // Push one max-size silent block through the filter so its internal
    // buffers reach full capacity now; std::vector keeps capacity on later
    // shrink/regrow, so the audio thread never triggers a reallocation.
    float* ptrs[1] = {scratch.data()};
    (void)dcBlocker.Process(ptrs, 1, maxBlock);

    modelSlot.prepare(sampleRate, maxBlockSize);
}

void Engine::process(float* samples, const int numFrames)
{
    // Hosts can exceed the promised block size (offline render, freeze).
    // Rule 1: chunk, never throw.
    int offset = 0;
    while (offset < numFrames)
    {
        const int n = std::min(numFrames - offset, maxBlock);
        processChunk(samples + offset, n);
        offset += n;
    }
}

void Engine::processChunk(float* samples, const int numFrames)
{
    float* inPtrs[1] = {samples};
    float* outPtrs[1] = {scratch.data()};

    if (ResamplingNam* model = modelSlot.render())
        model->process(inPtrs, outPtrs, numFrames);
    else
        std::memcpy(scratch.data(), samples, sizeof(float) * static_cast<size_t>(numFrames));

    // (IR convolution slots in here — milestone 2b.)

    float** dcOut = dcBlocker.Process(outPtrs, 1, numFrames);
    std::memcpy(samples, dcOut[0], sizeof(float) * static_cast<size_t>(numFrames));
}

} // namespace namrig::engine
