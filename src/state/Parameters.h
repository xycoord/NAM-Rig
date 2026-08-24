#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace namrig::state
{

// Stable string IDs. These are the contract with saved state — never reuse
// or repurpose one. New parameters get new IDs; removed ones are retired.
namespace param_ids
{
inline const juce::ParameterID inputGain{"input_gain", 1};
inline const juce::ParameterID outputGain{"output_gain", 1};
// 0..1 ratio fed to nam::SlimmableModel::SetSlimmableSize; 1 = full model.
// Shown to the user as "Quality".
inline const juce::ParameterID slim{"slim", 1};
// 0 = Raw, 1 = Normalized (level-match models via loudness metadata).
inline const juce::ParameterID outputMode{"output_mode", 1};
// IR convolution on/off (IR stays loaded while bypassed).
inline const juce::ParameterID irEnabled{"ir_enabled", 1};
// 0 = Auto (bus width in a DAW, mono standalone), 1 = Mono, 2 = Stereo.
inline const juce::ParameterID channels{"channels", 1};
// Policy for a 2ch IR under stereo processing: 0 = Dual mono (L*ch1, R*ch2),
// 1 = Mono -> stereo (collapse, then spread through the IR pair).
inline const juce::ParameterID stereoIrMode{"stereo_ir_mode", 1};
} // namespace param_ids

// Version stamped into every saved state blob from day one, so future
// releases can migrate old state with a single, explicit function.
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace namrig::state
