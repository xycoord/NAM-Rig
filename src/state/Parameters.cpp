#include "Parameters.h"

#include <cmath>

namespace namrig::state
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    // Frequencies read to 2 significant figures: "85 Hz", "3.4 kHz", "20 kHz".
    auto hzToText = [](float v, int) {
        double value = static_cast<double>(v);
        const double mag = std::pow(10.0, std::floor(std::log10(value)) - 1.0);
        value = std::round(value / mag) * mag;
        if (value >= 1000.0)
        {
            const double k = value / 1000.0;
            return (k >= 10.0 ? String{static_cast<int>(std::lround(k))} : String{k, 1})
                   + " kHz";
        }
        return String{static_cast<int>(std::lround(value))} + " Hz";
    };
    auto textToHz = [](const String& t) {
        const auto trimmed = t.trim().toLowerCase();
        const float num = trimmed.getFloatValue();
        return trimmed.contains("k") ? num * 1000.0f : num;
    };

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
    layout.add(std::make_unique<AudioParameterBool>(param_ids::ampEnabled, "Amp", true));

    layout.add(std::make_unique<AudioParameterFloat>(
        param_ids::verbSend, "Reverb Send", NormalisableRange<float>{-60.0f, 0.0f, 0.1f},
        -20.0f, AudioParameterFloatAttributes{}.withLabel("dB")));

    {
        NormalisableRange<float> r{20.0f, 120.0f, 0.1f};
        r.setSkewForCentre(50.0f);
        layout.add(std::make_unique<AudioParameterFloat>(
            param_ids::tight, "HPF", r, 20.0f,
            AudioParameterFloatAttributes{}
                .withStringFromValueFunction(hzToText)
                .withValueFromStringFunction(textToHz)));
    }
    {
        NormalisableRange<float> r{500.0f, 20000.0f, 1.0f};
        r.setSkewForCentre(3000.0f);
        layout.add(std::make_unique<AudioParameterFloat>(
            param_ids::tone, "LPF", r, 20000.0f,
            AudioParameterFloatAttributes{}
                .withStringFromValueFunction(hzToText)
                .withValueFromStringFunction(textToHz)));
    }

    layout.add(std::make_unique<AudioParameterChoice>(
        param_ids::channels, "Channels", StringArray{"Auto", "Mono", "Stereo"}, 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        param_ids::stereoIrMode, "Stereo IR", StringArray{"Dual mono", "Mono to stereo"}, 0));

    return layout;
}

} // namespace namrig::state
