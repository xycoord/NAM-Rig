#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace namrig::state
{

// Stable string IDs. These are the contract with saved state — never reuse
// or repurpose one. New parameters get new IDs; removed ones are retired.
// Retired IDs (never reuse): "input_gain", "output_gain" (replaced by the
// compensated drive/trim pair), "norm_target" (with input staging pinned to
// the meter's target zone, the bypass-matching offset is a derivable
// constant, not a user trim — see kNormTargetDb in PluginProcessor.cpp),
// "output_mode" (normalization is always on; models without loudness
// metadata pass through unadjusted, flagged in the UI).
namespace param_ids
{
// Gain into the model, inversely compensated at the output: changes how
// hard the model is driven at (near-enough) constant loudness.
inline const juce::ParameterID drive{"drive", 1};
// Input staging gain, applied before the meter tap: place the incoming
// signal in the meter's target zone once per rig.
inline const juce::ParameterID trim{"trim", 1};
// 0..1 ratio fed to nam::SlimmableModel::SetSlimmableSize; 1 = full model.
// Shown to the user as "Quality".
inline const juce::ParameterID slim{"slim", 1};
// IR convolution on/off (IR stays loaded while bypassed).
inline const juce::ParameterID irEnabled{"ir_enabled", 1};
// Amp section on/off (filters + drive + model; trim and IR unaffected).
inline const juce::ParameterID ampEnabled{"amp_enabled", 1};
// 0 = Auto (bus width in a DAW, mono standalone), 1 = Mono, 2 = Stereo.
inline const juce::ParameterID channels{"channels", 1};
// Pre-amp filters (part of the sound; in presets).
// Tight: 12 dB/oct high-pass, 20 Hz (~off) .. 120 Hz — pre-gain low-end control.
inline const juce::ParameterID tight{"tight", 1};
// Tone: 6 dB/oct low-pass like a guitar tone pot, 500 Hz .. 20 kHz (open = bypass).
inline const juce::ParameterID tone{"tone", 1};
// Policy for a 2ch IR under stereo processing: 0 = Dual mono (L*ch1, R*ch2),
// 1 = Mono -> stereo (collapse, then spread through the IR pair).
inline const juce::ParameterID stereoIrMode{"stereo_ir_mode", 1};
} // namespace param_ids

// Version stamped into every saved state blob from day one, so future
// releases can migrate old state with a single, explicit function.
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace namrig::state
