#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace namrig
{

class Processor;

// Milestone 1 editor: two gain knobs, resizable, flat vector theme.
// The real layout work happens in milestone 4; this exists to prove the
// windowing, scaling, and parameter attachment paths.
class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor(Processor&);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Slider inputSlider, outputSlider;
    juce::Label inputLabel, outputLabel;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;

    // Standalone only: opens the audio device settings (the native title bar
    // has no room for JUCE's Options button, and a rig needs this one click
    // away). Hidden in plugin builds, where the host owns device config.
    juce::TextButton settingsButton{"Audio Settings"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace namrig
