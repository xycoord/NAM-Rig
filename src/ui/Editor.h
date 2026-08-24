#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

namespace namrig
{

class Processor;

// Interim editor, structured as the signal chain reads: INPUT -> AMP ->
// CAB/IR -> OUTPUT as labeled sections, with routing and app settings in a
// utility bar. The real theme, meters, and preset bar arrive in milestone 4;
// this establishes the layout language they land in.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseModel();
    void chooseIr();

    Processor& processor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Section frames, computed in resized(), drawn in paint().
    struct Section
    {
        juce::Rectangle<int> frame;
        const char* title = "";
    };
    std::array<Section, 4> sections;

    // Utility bar.
    juce::ComboBox channelsBox;
    juce::Label channelsCaption;
    juce::Label topologyLabel;
    juce::TextButton settingsButton{"Audio Settings"}; // standalone only
    std::unique_ptr<ComboAttachment> channelsAttachment;

    // INPUT.
    juce::Slider inputSlider;
    std::unique_ptr<SliderAttachment> inputAttachment;

    // AMP.
    juce::TextButton loadModelButton{"Load..."};
    juce::TextButton clearModelButton{"Clear"};
    juce::Label modelStatusLabel;
    juce::Label qualityCaption;
    juce::Slider qualitySlider;
    std::unique_ptr<SliderAttachment> qualityAttachment;

    // CAB / IR.
    juce::TextButton loadIrButton{"Load..."};
    juce::TextButton clearIrButton{"Clear"};
    juce::ToggleButton irToggle{"Enabled"};
    juce::Label irStatusLabel;
    juce::ComboBox stereoIrBox; // visible only when it means something
    std::unique_ptr<ButtonAttachment> irToggleAttachment;
    std::unique_ptr<ComboAttachment> stereoIrAttachment;

    // OUTPUT.
    juce::Slider outputSlider;
    juce::ComboBox outputModeBox;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<ComboAttachment> outputModeAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace namrig
