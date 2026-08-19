#include "Parameters.h"

namespace namrig::state
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    auto gainRange = NormalisableRange<float>{-40.0f, 40.0f, 0.1f};

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::inputGain, "Input",
        NormalisableRange<float>{-20.0f, 20.0f, 0.1f}, 0.0f,
        AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::outputGain, "Output", gainRange, 0.0f,
        AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::slim, "Quality", NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 1.0f));

    layout.add(std::make_unique<AudioParameterChoice>(
        param_ids::outputMode, "Output Mode", StringArray{"Raw", "Normalized"}, 1));

    layout.add(std::make_unique<AudioParameterBool>(param_ids::irEnabled, "IR", true));

    return layout;
}

} // namespace namrig::state
