#include "Editor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginProcessor.h"
#include "state/Parameters.h"

namespace namrig
{

namespace
{
// Placeholder palette; the real theme arrives in milestone 4.
const juce::Colour kBackground{0xff1d1a1f};
const juce::Colour kText{0xffe6e8ec};
} // namespace

Editor::Editor(Processor& p) : AudioProcessorEditor(p)
{
    auto setUpKnob = [this](juce::Slider& slider, juce::Label& label, const char* name) {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 22);
        slider.setTextValueSuffix(" dB");
        addAndMakeVisible(slider);

        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(label);
    };

    setUpKnob(inputSlider, inputLabel, "Input");
    setUpKnob(outputSlider, outputLabel, "Output");

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        settingsButton.onClick = [] {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->showAudioSettingsDialog();
        };
        addAndMakeVisible(settingsButton);
    }

    auto& state = p.getState();
    inputAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::inputGain.getParamID(), inputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::outputGain.getParamID(), outputSlider);

    setResizable(true, true);
    // Minimum only — no maximum, so maximized/fullscreen windows fill.
    setResizeLimits(320, 200, 0x3fffffff, 0x3fffffff);
    setSize(480, 280);
}

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
}

void Editor::resized()
{
    using Fb = juce::FlexBox;
    using Fi = juce::FlexItem;

    auto area = getLocalBounds().reduced(20);

    if (settingsButton.isVisible())
        settingsButton.setBounds(area.removeFromBottom(28).removeFromRight(120));

    auto bounds = area.toFloat();

    Fb row;
    row.flexDirection = Fb::Direction::row;
    row.justifyContent = Fb::JustifyContent::spaceAround;
    row.alignItems = Fb::AlignItems::stretch;

    auto knobColumn = [](juce::Slider& slider, juce::Label& label) {
        auto column = std::make_unique<Fb>();
        column->flexDirection = Fb::Direction::column;
        column->items.add(Fi(label).withHeight(24.0f));
        column->items.add(Fi(slider).withFlex(1.0f));
        return column;
    };

    // FlexBox doesn't own nested boxes; lay out the two columns directly.
    auto inputCol = knobColumn(inputSlider, inputLabel);
    auto outputCol = knobColumn(outputSlider, outputLabel);

    row.items.add(Fi(*inputCol).withFlex(1.0f).withMaxWidth(220.0f));
    row.items.add(Fi(*outputCol).withFlex(1.0f).withMaxWidth(220.0f));
    row.performLayout(bounds);
}

} // namespace namrig
