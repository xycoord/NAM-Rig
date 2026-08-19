#include "PluginProcessor.h"

#include <cmath>
#include <cstdlib>

#include "ui/Editor.h"

namespace namrig
{

namespace
{
// Normalized mode targets this loudness for models that carry metadata.
constexpr double kTargetLoudnessDb = -18.0;
} // namespace

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "state", state::createParameterLayout())
{
    inputGainDb = state.getRawParameterValue(state::param_ids::inputGain.getParamID());
    outputGainDb = state.getRawParameterValue(state::param_ids::outputGain.getParamID());
    slimParam = state.getRawParameterValue(state::param_ids::slim.getParamID());
    outputModeParam = state.getRawParameterValue(state::param_ids::outputMode.getParamID());

    // Development convenience until the file browser exists (milestone 4):
    // NAMRIG_MODEL=/path/to/model.nam. The loader parks the request until
    // the engine is prepared.
    if (const char* modelPath = std::getenv("NAMRIG_MODEL"))
        engine.models().requestLoad(std::filesystem::path{modelPath});

    startTimerHz(10);
}

Processor::~Processor()
{
    stopTimer();
}

void Processor::prepareToPlay(const double sampleRate, const int maximumExpectedSamplesPerBlock)
{
    preparedBlockSize = maximumExpectedSamplesPerBlock;
    monoBuffer.assign(static_cast<size_t>(preparedBlockSize), 0.0f);

    engine.prepare(sampleRate, preparedBlockSize);

    const double rampSeconds = 0.02;
    inputGain.reset(sampleRate, rampSeconds);
    outputGain.reset(sampleRate, rampSeconds);
    inputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inputGainDb->load()));
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        outputGainDb->load() + normalizationOffsetDb.load(std::memory_order_relaxed)));
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalFrames = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int numIn = juce::jmin(getTotalNumInputChannels(), numChannels);
    const int numOut = juce::jmin(getTotalNumOutputChannels(), numChannels);
    const float inputScale = numIn > 0 ? 1.0f / static_cast<float>(numIn) : 0.0f;

    inputGain.setTargetValue(juce::Decibels::decibelsToGain(inputGainDb->load()));
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(
        outputGainDb->load() + normalizationOffsetDb.load(std::memory_order_relaxed)));

    // Hosts may exceed the promised block size; monoBuffer is sized to the
    // promise, so chunk here (the engine also chunks internally — belt and
    // braces, both allocation-free).
    for (int offset = 0; offset < totalFrames; offset += preparedBlockSize)
    {
        const int n = juce::jmin(totalFrames - offset, preparedBlockSize);

        // Mix down with smoothed input gain.
        for (int s = 0; s < n; ++s)
        {
            float mono = 0.0f;
            for (int c = 0; c < numIn; ++c)
                mono += buffer.getReadPointer(c)[offset + s];
            monoBuffer[static_cast<size_t>(s)] = mono * inputScale * inputGain.getNextValue();
        }

        engine.process(monoBuffer.data(), n);

        // Broadcast with smoothed output gain.
        for (int s = 0; s < n; ++s)
        {
            const float out = monoBuffer[static_cast<size_t>(s)] * outputGain.getNextValue();
            for (int c = 0; c < numOut; ++c)
                buffer.getWritePointer(c)[offset + s] = out;
        }
    }
}

void Processor::timerCallback()
{
    // Latency: flagged by the audio thread's model swap, applied here
    // (message thread) so the host is never called from the callback.
    const int latency = engine.latencySamples();
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    // Output-mode normalization from model metadata.
    const auto info = engine.models().info();
    const bool normalized = outputModeParam->load() >= 0.5f;
    const float offset = (normalized && info.hasLoudness)
                             ? static_cast<float>(kTargetLoudnessDb - info.loudness)
                             : 0.0f;
    normalizationOffsetDb.store(offset, std::memory_order_relaxed);

    // Forward Quality (slim) changes; the loader thread applies them.
    const float slim = slimParam->load();
    if (std::abs(slim - lastForwardedSlim) > 1.0e-6f)
    {
        lastForwardedSlim = slim;
        engine.models().setSlim(slim);
    }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}

void Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty("stateVersion", state::kStateVersion, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void Processor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace namrig

// JUCE entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new namrig::Processor();
}
