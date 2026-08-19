#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "state/Parameters.h"

namespace namrig
{

// Milestone 1: passthrough with smoothed input/output gain.
// The NAM engine slots in between the two gains in milestone 2.
class Processor final : public juce::AudioProcessor
{
public:
    Processor();

    // AudioProcessor
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NAM Rig"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Programs: JUCE requires at least one.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State (milestone 3 replaces this with the versioned serializer;
    // the version field is present from the very first blob).
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }

private:
    juce::AudioProcessorValueTreeState state;

    // Raw parameter pointers (atomic reads on the audio thread).
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;

    // Rule: every gain is smoothed. 20 ms linear ramp.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain, outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace namrig
