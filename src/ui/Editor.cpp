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
const juce::Colour kError{0xffe38a82};
const juce::Colour kDim{0xff85818f};
} // namespace

Editor::Editor(Processor& p) : AudioProcessorEditor(p), processor(p)
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

    auto& state = processor.getState();
    inputAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::inputGain.getParamID(), inputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::outputGain.getParamID(), outputSlider);

    loadModelButton.onClick = [this] { chooseModel(); };
    addAndMakeVisible(loadModelButton);

    clearModelButton.onClick = [this] { processor.getEngine().models().requestClear(); };
    addAndMakeVisible(clearModelButton);

    modelStatusLabel.setJustificationType(juce::Justification::centredLeft);
    modelStatusLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(modelStatusLabel);

    loadIrButton.onClick = [this] { chooseIr(); };
    addAndMakeVisible(loadIrButton);

    clearIrButton.onClick = [this] { processor.clearIr(); };
    addAndMakeVisible(clearIrButton);

    irToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state, state::param_ids::irEnabled.getParamID(), irToggle);
    addAndMakeVisible(irToggle);

    irStatusLabel.setJustificationType(juce::Justification::centredLeft);
    irStatusLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(irStatusLabel);

    auto setUpChoiceBox = [this, &state](juce::ComboBox& box, const juce::ParameterID& id,
                                         auto& attachment) {
        if (auto* p2 = state.getParameter(id.getParamID()))
            box.addItemList(p2->getAllValueStrings(), 1);
        addAndMakeVisible(box);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, id.getParamID(), box);
    };
    setUpChoiceBox(channelsBox, state::param_ids::channels, channelsAttachment);
    setUpChoiceBox(stereoIrBox, state::param_ids::stereoIrMode, stereoIrAttachment);

    topologyLabel.setJustificationType(juce::Justification::centredRight);
    topologyLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(topologyLabel);

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        settingsButton.onClick = [] {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->showAudioSettingsDialog();
        };
        addAndMakeVisible(settingsButton);
    }

    setResizable(true, true);
    // Minimum only — no maximum, so maximized/fullscreen windows fill.
    setResizeLimits(360, 240, 0x3fffffff, 0x3fffffff);
    setSize(520, 320);

    startTimerHz(4); // status label refresh
    timerCallback();
}

Editor::~Editor() = default;

void Editor::timerCallback()
{
    const auto info = processor.getEngine().models().info();

    if (!info.error.empty())
    {
        modelStatusLabel.setColour(juce::Label::textColourId, kError);
        modelStatusLabel.setText("Load failed: " + juce::String(info.error),
                                 juce::dontSendNotification);
    }
    else if (info.loaded)
    {
        modelStatusLabel.setColour(juce::Label::textColourId, kText);
        juce::String text = juce::File(info.path).getFileName();
        if (info.slimmable)
            text += "  (slimmable)";
        modelStatusLabel.setText(text, juce::dontSendNotification);
    }
    else
    {
        modelStatusLabel.setColour(juce::Label::textColourId, kDim);
        modelStatusLabel.setText("No model loaded", juce::dontSendNotification);
    }

    clearModelButton.setEnabled(info.loaded);

    if (processor.isIrLoaded())
    {
        irStatusLabel.setColour(juce::Label::textColourId, kText);
        irStatusLabel.setText(juce::File(processor.getIrPath()).getFileName(),
                              juce::dontSendNotification);
    }
    else
    {
        irStatusLabel.setColour(juce::Label::textColourId, kDim);
        irStatusLabel.setText("No IR loaded", juce::dontSendNotification);
    }
    clearIrButton.setEnabled(processor.isIrLoaded());

    topologyLabel.setText(processor.topologyDescription(), juce::dontSendNotification);
    // The stereo-IR policy only matters for a 2ch IR under stereo processing.
    stereoIrBox.setEnabled(processor.isIrLoaded());
}

void Editor::chooseModel()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load NAM model", juce::File{}, "*.nam");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) {
                                 const auto file = fc.getResult();
                                 if (file.existsAsFile())
                                     processor.getEngine().models().requestLoad(
                                         file.getFullPathName().toStdString());
                             });
}

void Editor::chooseIr()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load impulse response", juce::File{},
                                                      "*.wav;*.aif;*.aiff;*.flac");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) {
                                 const auto file = fc.getResult();
                                 if (file.existsAsFile())
                                     processor.loadIr(file);
                             });
}

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
}

void Editor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // Routing row: channel mode, stereo-IR policy, resolved topology.
    auto routingBar = area.removeFromBottom(26);
    channelsBox.setBounds(routingBar.removeFromLeft(110));
    routingBar.removeFromLeft(8);
    stereoIrBox.setBounds(routingBar.removeFromLeft(130));
    routingBar.removeFromLeft(12);
    topologyLabel.setBounds(routingBar);
    area.removeFromBottom(8);

    // Bottom bars: IR row above model row (both interim until milestone 4).
    auto irBar = area.removeFromBottom(28);
    loadIrButton.setBounds(irBar.removeFromLeft(110));
    irBar.removeFromLeft(8);
    clearIrButton.setBounds(irBar.removeFromLeft(60));
    irBar.removeFromLeft(12);
    irToggle.setBounds(irBar.removeFromLeft(60));
    irBar.removeFromLeft(12);
    irStatusLabel.setBounds(irBar);
    area.removeFromBottom(8);

    auto modelBar = area.removeFromBottom(28);
    loadModelButton.setBounds(modelBar.removeFromLeft(110));
    modelBar.removeFromLeft(8);
    clearModelButton.setBounds(modelBar.removeFromLeft(60));
    modelBar.removeFromLeft(12);
    if (settingsButton.isVisible())
    {
        settingsButton.setBounds(modelBar.removeFromRight(120));
        modelBar.removeFromRight(12);
    }
    modelStatusLabel.setBounds(modelBar);
    area.removeFromBottom(12);

    using Fb = juce::FlexBox;
    using Fi = juce::FlexItem;

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

    auto inputCol = knobColumn(inputSlider, inputLabel);
    auto outputCol = knobColumn(outputSlider, outputLabel);

    row.items.add(Fi(*inputCol).withFlex(1.0f).withMaxWidth(220.0f));
    row.items.add(Fi(*outputCol).withFlex(1.0f).withMaxWidth(220.0f));
    row.performLayout(area.toFloat());
}

} // namespace namrig
