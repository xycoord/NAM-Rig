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
} // namespace param_ids

// Version stamped into every saved state blob from day one, so future
// releases can migrate old state with a single, explicit function.
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace namrig::state
