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
    void paintOverChildren(juce::Graphics&) override;
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

    // Small square button with a path-drawn icon (text glyphs sit on the
    // font baseline and never center properly at this size).
    class IconButton final : public juce::Button
    {
    public:
        enum class Kind { prev, next, clear };
        explicit IconButton(Kind k) : juce::Button({}), kind(k) {}

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced(0.5f);
            auto fill = theme::colours::raised;
            if (down)
                fill = theme::colours::accentDim;
            else if (highlighted && isEnabled())
                fill = fill.brighter(0.08f);
            g.setColour(fill);
            g.fillRoundedRectangle(r, 4.0f);
            g.setColour(theme::colours::outline);
            g.drawRoundedRectangle(r, 4.0f, 1.0f);

            const auto c = r.getCentre();
            const float s = 3.5f;
            juce::Path p;
            switch (kind)
            {
                case Kind::prev:
                    p.startNewSubPath(c.x + s * 0.6f, c.y - s);
                    p.lineTo(c.x - s * 0.6f, c.y);
                    p.lineTo(c.x + s * 0.6f, c.y + s);
                    break;
                case Kind::next:
                    p.startNewSubPath(c.x - s * 0.6f, c.y - s);
                    p.lineTo(c.x + s * 0.6f, c.y);
                    p.lineTo(c.x - s * 0.6f, c.y + s);
                    break;
                case Kind::clear:
                    p.startNewSubPath(c.x - s, c.y - s);
                    p.lineTo(c.x + s, c.y + s);
                    p.startNewSubPath(c.x + s, c.y - s);
                    p.lineTo(c.x - s, c.y + s);
                    break;
            }
            g.setColour(isEnabled() ? theme::colours::textPrimary
                                    : theme::colours::textSecondary.withAlpha(0.5f));
            g.strokePath(p, juce::PathStrokeType{1.6f, juce::PathStrokeType::mitered,
                                                 juce::PathStrokeType::rounded});
        }

    private:
        Kind kind;
    };

    // Header power switch: classic power glyph, accent when on.
    class PowerButton final : public juce::Button
    {
    public:
        PowerButton() : juce::Button({}) { setClickingTogglesState(true); }
        void paintButton(juce::Graphics& g, bool highlighted, bool) override
        {
            const auto on = getToggleState();
            auto colour = on ? theme::colours::accent
                             : theme::colours::textSecondary.withAlpha(0.55f);
            if (highlighted)
                colour = colour.brighter(0.15f);
            g.setColour(colour);
            const auto b = getLocalBounds().toFloat().reduced(3.0f);
            const auto c = b.getCentre();
            const float radius = juce::jmin(b.getWidth(), b.getHeight()) / 2.0f - 1.0f;
            juce::Path arc;
            arc.addCentredArc(c.x, c.y, radius, radius, 0.0f,
                              juce::MathConstants<float>::pi * 0.22f,
                              juce::MathConstants<float>::pi * 1.78f, true);
            g.strokePath(arc, juce::PathStrokeType{1.8f});
            g.drawLine(c.x, c.y - radius - 1.0f, c.x, c.y - radius * 0.15f, 1.8f);
        }
    };

    // Selector row: prev / name-as-button / next / clear. The name opens
    // the picker; arrows step through the current folder while playing.
    struct SelectorRow
    {
        IconButton prev{IconButton::Kind::prev}, next{IconButton::Kind::next},
            clear{IconButton::Kind::clear};
        juce::TextButton name;
        void addTo(juce::Component& parent)
        {
            parent.addAndMakeVisible(prev);
            parent.addAndMakeVisible(name);
            parent.addAndMakeVisible(next);
            parent.addAndMakeVisible(clear);
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
    juce::Label tightCaption, toneCaption, driveCaption;
    juce::Slider tightSlider, toneSlider, driveSlider;
    std::unique_ptr<SliderAttachment> tightAttachment, toneAttachment;
    juce::Label qualityCaption;
    juce::ComboBox qualityBox; // discrete levels from the model's breakpoints
    std::vector<double> qualityLevelValues; // slim ratio per dropdown item
    std::vector<double> shownBreakpoints;   // cache: rebuild only on change
    std::unique_ptr<SliderAttachment> driveAttachment;
    void rebuildQualityLevels(const std::vector<double>& breakpoints);
    void syncQualitySelection();

    PowerButton ampPower, irPower;
    std::unique_ptr<ButtonAttachment> ampPowerAttachment, irPowerAttachment;

    // CAB / IR.
    SelectorRow irRow;
    juce::ComboBox stereoIrBox; // visible only when it means something
    std::unique_ptr<ButtonAttachment> irToggleAttachment;
    std::unique_ptr<ComboAttachment> stereoIrAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace namrig
