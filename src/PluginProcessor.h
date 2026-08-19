#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "engine/Engine.h"
#include "state/Parameters.h"

namespace namrig
{

// JUCE shell around engine::Engine. Owns the gains (SmoothedValues), the
// mono mixdown/broadcast, and the message-thread duties the engine can't do
// itself: latency reporting to the host, output-mode normalization, and
// forwarding the Quality (slim) parameter — all via a 10 Hz timer, never
// from the audio thread (CLAUDE.md rules 3 & 4).
class Processor final : public juce::AudioProcessor, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // don't hide the double overload
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NAM Rig"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // DC blocker decay: 10 cycles at 5 Hz, matching the old plugin's tail.
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }
    engine::Engine& getEngine() { return engine; }

    // IR management (message thread). juce::dsp::Convolution loads on its
    // own background thread and swaps lock-free under a running process().
    void loadIr(const juce::File& file);
    void clearIr();
    juce::String getIrPath() const { return irPath; }
    bool isIrLoaded() const { return irLoaded.load(std::memory_order_relaxed); }

private:
    void timerCallback() override; // message thread

    juce::AudioProcessorValueTreeState state;
    engine::Engine engine;

    // Raw parameter pointers (atomic reads on the audio thread).
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* slimParam = nullptr;
    std::atomic<float>* outputModeParam = nullptr;

    // Output-mode offset, computed on the message thread from model
    // metadata, folded into the smoothed output gain on the audio thread.
    std::atomic<float> normalizationOffsetDb{0.0f};

    // Rule: every gain is smoothed.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain, outputGain;

    // IR stage. The toggle is a smoothed wet/dry crossfade, not a hard
    // switch — no click on bypass.
    juce::dsp::Convolution convolution;
    std::atomic<float>* irEnabledParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> irMix;
    juce::String irPath;
    std::atomic<bool> irLoaded{false};

    std::vector<float> monoBuffer, dryBuffer; // sized in prepareToPlay
    int preparedBlockSize = 0;

    float lastForwardedSlim = -1.0f; // timer-side cache

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace namrig
