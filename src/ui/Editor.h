#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace namrig
{

class Processor;

// Milestone 2 editor: gains, a minimal model loader (button + status line —
// the real browser with folder-stepping is milestone 4), and the standalone
// settings button. Resizable, flat vector theme.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override; // poll model status for the label
    void chooseModel();
    void chooseIr();

    Processor& processor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Slider inputSlider, outputSlider;
    juce::Label inputLabel, outputLabel;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;

    juce::TextButton loadModelButton{"Load model..."};
    juce::TextButton clearModelButton{"Clear"};
    juce::Label modelStatusLabel;

    juce::TextButton loadIrButton{"Load IR..."};
    juce::TextButton clearIrButton{"Clear"};
    juce::ToggleButton irToggle{"IR"};
    juce::Label irStatusLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> irToggleAttachment;

    juce::ComboBox channelsBox, stereoIrBox;
    juce::Label topologyLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelsAttachment,
        stereoIrAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Standalone only: opens the audio device settings.
    juce::TextButton settingsButton{"Audio Settings"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace namrig
