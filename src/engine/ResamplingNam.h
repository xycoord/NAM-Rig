#pragma once

#include <cmath>
#include <functional>
#include <memory>

#include "AdtPrelude.h"

#include "NAM/dsp.h"
#include "NAM/slimmable.h"
#include "dsp/ResamplingContainer/ResamplingContainer.h"

namespace namrig::engine
{

// The sample rate a model was captured at. Models from before the format
// embedded a sample rate report <= 0; the community assumption is that those
// are 48k, which is almost always right.
double modelSampleRate(const nam::DSP& model);

// Wraps a loaded NAM model and transparently resamples between the external
// (host/device) rate and the model's native rate, forwarding the model's
// level metadata so calibration code upstream doesn't need to know
// resampling exists. Port of the old plugin's ResamplingNAM.
//
// Threading: construction, Reset() and setSlim() happen off the audio
// thread; process() happens on it. Slim changes are safe concurrent with
// process() — the core stages the rebuilt model internally and installs it
// at the top of its own process().
class ResamplingNam final : public nam::DSP
{
public:
    // Takes ownership. Resets (and therefore prewarms) at the external rate.
    ResamplingNam(std::unique_ptr<nam::DSP> encapsulated, double externalSampleRate,
                  int maxBlockSize);

    void prewarm() override { encapsulated->prewarm(); }
    void process(NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames) override;
    void Reset(double sampleRate, int maxBlockSize) override;

    // 0 when the external rate matches the model's native rate.
    int latency() const { return needsResampling() ? resampler.GetLatency() : 0; }

    double encapsulatedSampleRate() const { return modelSampleRate(*encapsulated); }

    nam::SlimmableModel* slimmable()
    {
        return dynamic_cast<nam::SlimmableModel*>(encapsulated.get());
    }

private:
    bool needsResampling() const
    {
        return std::fabs(GetExpectedSampleRate() - encapsulatedSampleRate()) > 1.0e-6;
    }

    std::unique_ptr<nam::DSP> encapsulated;
    dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> resampler;
    int maxExternalBlockSize = 0;
    std::function<void(NAM_SAMPLE**, NAM_SAMPLE**, int)> blockProcessFunc;
};

} // namespace namrig::engine
