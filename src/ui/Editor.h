#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace namrig
{

class Processor;

// Interim editor, structured as the signal chain reads: INPUT (trim + staging
// meter) -> AMP (model + drive + quality) -> CAB/IR -> OUTPUT (mode + level),
// with routing and app settings in a utility bar. The real theme arrives in
// milestone 4; this establishes the layout language it lands in.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Chromatic tuner strip across the top. Note is the hero; deviation is
    // a bar growing from centre (right = sharp); green ONLY when in tune
    // (same semantic as the meter's target zone). Idle = quiet dash.
    class TunerStrip final : public juce::Component
    {
    public:
        void setReading(float freqHz, float clarity); // UI thread, per tick
        void paint(juce::Graphics&) override;

    private:
        int noteIndex = -1;    // midi note, -1 = idle
        float centsSmoothed = 0.0f;
        juce::int64 lastGoodMs = 0;
        bool inTune = false;
    };

    // Input staging meter: post-trim peak against a target zone. Repaints
    // only itself (rule 8).
    class StagingMeter final : public juce::Component
    {
    public:
        explicit StagingMeter(bool withTargetZone = true) : showZone(withTargetZone) {}
        void setLevel(float peakLinear); // UI thread
        void paint(juce::Graphics&) override;

    private:
        bool showZone;
        float levelDb = -60.0f;
        bool clipped = false;
    };

    void timerCallback() override;
    void chooseModel();
    void chooseIr();

    Processor& processor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Section
    {
        juce::Rectangle<int> frame;
        const char* title = "";
    };
    std::array<Section, 4> sections;

    TunerStrip tunerStrip;

    // Utility bar.
    juce::ComboBox presetBox;
    juce::TextEditor presetNameEditor; // inline save: replaces the box briefly
    juce::TextButton savePresetButton{"Save..."};
    juce::TextButton deletePresetButton{"Delete"};
    bool deleteArmed = false;
    juce::Label topologyLabel;
    juce::TextButton settingsButton{"Audio Settings"}; // standalone only
    void refreshPresetList();
    void promptSavePreset();

    // INPUT: staging meter + trim + channels. OUT: output peak strip.
    StagingMeter meter;
    StagingMeter outputMeter{false};
    juce::Label trimCaption;
    juce::Slider trimSlider;
    juce::Label channelsCaption;
    juce::ComboBox channelsBox;
    std::unique_ptr<SliderAttachment> trimAttachment;
    std::unique_ptr<ComboAttachment> channelsAttachment;

    // Selector row: prev / name-as-button / next / clear. The name opens
    // the picker; arrows step through the current folder while playing.
    struct SelectorRow
    {
        juce::TextButton prev{juce::CharPointer_UTF8{"\xe2\x80\xb9"}},
            next{juce::CharPointer_UTF8{"\xe2\x80\xba"}},
            name, clear{juce::CharPointer_UTF8{"\xe2\x9c\x95"}};
        void addTo(juce::Component& parent)
        {
            for (auto* b : {&prev, &name, &next, &clear})
                parent.addAndMakeVisible(*b);
        }
        void layout(juce::Rectangle<int> row)
        {
            prev.setBounds(row.removeFromLeft(26));
            row.removeFromLeft(4);
            clear.setBounds(row.removeFromRight(26));
            row.removeFromRight(4);
            next.setBounds(row.removeFromRight(26));
            row.removeFromRight(4);
            name.setBounds(row);
        }
    };
    void stepModel(int delta);
    void stepIr(int delta);

    // AMP: model + drive + quality.
    SelectorRow modelRow;
    juce::Label normStatusLabel; // normalization offset / missing-metadata flag
    juce::Label driveCaption;
    juce::Slider driveSlider;
    juce::Label qualityCaption;
    juce::ComboBox qualityBox; // discrete levels from the model's breakpoints
    std::vector<double> qualityLevelValues; // slim ratio per dropdown item
    std::vector<double> shownBreakpoints;   // cache: rebuild only on change
    std::unique_ptr<SliderAttachment> driveAttachment;
    void rebuildQualityLevels(const std::vector<double>& breakpoints);
    void syncQualitySelection();

    // CAB / IR.
    SelectorRow irRow;
    juce::ToggleButton irToggle{"Enabled"};
    juce::ComboBox stereoIrBox; // visible only when it means something
    std::unique_ptr<ButtonAttachment> irToggleAttachment;
    std::unique_ptr<ComboAttachment> stereoIrAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace namrig
