#include "Parameters.h"

namespace namrig::state
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::drive, "Drive", NormalisableRange<float>{-20.0f, 20.0f, 0.1f}, 0.0f,
        AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::trim, "Input Trim", NormalisableRange<float>{-24.0f, 24.0f, 0.1f}, 0.0f,
        AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::slim, "Quality", NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 1.0f));

    layout.add(std::make_unique<AudioParameterBool>(param_ids::irEnabled, "IR", true));

    layout.add(std::make_unique<AudioParameterChoice>(
        param_ids::channels, "Channels", StringArray{"Auto", "Mono", "Stereo"}, 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        param_ids::stereoIrMode, "Stereo IR", StringArray{"Dual mono", "Mono to stereo"}, 0));

    return layout;
}

} // namespace namrig::state
