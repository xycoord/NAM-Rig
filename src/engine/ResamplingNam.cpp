#include "ResamplingNam.h"

#include <cassert>
#include <cmath>

namespace namrig::engine
{

double modelSampleRate(const nam::DSP& model)
{
    const double reported = model.GetExpectedSampleRate();
    return reported <= 0.0 ? 48000.0 : reported;
}

ResamplingNam::ResamplingNam(std::unique_ptr<nam::DSP> enc, double externalSampleRate,
                             int maxBlockSize)
    : nam::DSP(enc->NumInputChannels(), enc->NumOutputChannels(), externalSampleRate),
      encapsulated(std::move(enc)),
      resampler(modelSampleRate(*encapsulated))
{
    blockProcessFunc = [this](NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames) {
        encapsulated->process(input, output, numFrames);
    };

    // Forward level metadata so the outside world can treat this wrapper as
    // the model itself.
    if (encapsulated->HasLoudness())
        SetLoudness(encapsulated->GetLoudness());
    if (encapsulated->HasInputLevel())
        SetInputLevel(encapsulated->GetInputLevel());
    if (encapsulated->HasOutputLevel())
        SetOutputLevel(encapsulated->GetOutputLevel());

    Reset(externalSampleRate, maxBlockSize);
}

void ResamplingNam::process(NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames)
{
    // The engine chunks blocks to the prepared maximum; anything larger here
    // is a programming error upstream, not a runtime condition. Never throw
    // on the audio thread.
    assert(numFrames <= maxExternalBlockSize);

    if (!needsResampling())
        encapsulated->process(input, output, numFrames);
    else
        resampler.ProcessBlock(input, output, numFrames, blockProcessFunc);
}

void ResamplingNam::Reset(const double sampleRate, const int maxBlockSize)
{
    mExpectedSampleRate = sampleRate;
    maxExternalBlockSize = maxBlockSize;
    resampler.Reset(sampleRate, maxBlockSize);

    // Size the encapsulated model for the largest block the resampler will
    // hand it. (Resets and prewarms — off the audio thread only.)
    const double upRatio = sampleRate / encapsulatedSampleRate();
    const auto maxEncapsulatedBlockSize =
        static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / upRatio));
    encapsulated->Reset(encapsulatedSampleRate(), maxEncapsulatedBlockSize);
}

} // namespace namrig::engine
