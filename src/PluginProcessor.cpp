#include "PluginProcessor.h"

#include "ui/Editor.h"

namespace namrig
{

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "state", state::createParameterLayout())
{
    inputGainDb = state.getRawParameterValue(state::param_ids::inputGain.getParamID());
    outputGainDb = state.getRawParameterValue(state::param_ids::outputGain.getParamID());
}

void Processor::prepareToPlay(double sampleRate, int)
{
    const double rampSeconds = 0.02;
    inputGain.reset(sampleRate, rampSeconds);
    outputGain.reset(sampleRate, rampSeconds);
    inputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inputGainDb->load()));
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputGainDb->load()));
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Mono or stereo, and no in/out mismatch surprises.
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

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    inputGain.setTargetValue(juce::Decibels::decibelsToGain(inputGainDb->load()));
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputGainDb->load()));

    // Mono-internal, like the engine will be: average the connected inputs
    // to mono, then broadcast to every output. A mono interface input is
    // heard in both ears; stereo input doesn't double in loudness.
    const int numIn = juce::jmin(getTotalNumInputChannels(), numChannels);
    const int numOut = juce::jmin(getTotalNumOutputChannels(), numChannels);
    const float inputScale = numIn > 0 ? 1.0f / static_cast<float>(numIn) : 0.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        float mono = 0.0f;
        for (int c = 0; c < numIn; ++c)
            mono += buffer.getReadPointer(c)[s];
        mono *= inputScale * inputGain.getNextValue();

        // The NAM engine goes here (milestone 2).

        const float out = mono * outputGain.getNextValue();
        for (int c = 0; c < numOut; ++c)
            buffer.getWritePointer(c)[s] = out;
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
