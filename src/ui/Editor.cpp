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
const juce::Colour kPanel{0xff232028};
const juce::Colour kFrame{0xff37333e};
const juce::Colour kText{0xffe6e8ec};
const juce::Colour kError{0xffe38a82};
const juce::Colour kDim{0xff85818f};
const juce::Colour kAccent{0xff7fa8f2};
} // namespace

Editor::Editor(Processor& p) : AudioProcessorEditor(p), processor(p)
{
    auto& state = processor.getState();

    auto attachCombo = [&](juce::ComboBox& box, const juce::ParameterID& id,
                           std::unique_ptr<ComboAttachment>& attachment) {
        if (auto* param = state.getParameter(id.getParamID()))
            box.addItemList(param->getAllValueStrings(), 1);
        addAndMakeVisible(box);
        attachment = std::make_unique<ComboAttachment>(state, id.getParamID(), box);
    };

    // --- utility bar ---
    attachCombo(channelsBox, state::param_ids::channels, channelsAttachment);
    channelsBox.setTooltip("Processing width. Auto follows the input bus in a DAW "
                           "and stays mono in the standalone.");

    topologyLabel.setJustificationType(juce::Justification::centred);
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

    // --- INPUT ---
    auto setUpKnob = [this](juce::Slider& slider) {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
        slider.setTextValueSuffix(" dB");
        addAndMakeVisible(slider);
    };
    setUpKnob(inputSlider);
    inputAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::inputGain.getParamID(), inputSlider);

    // --- AMP ---
    loadModelButton.onClick = [this] { chooseModel(); };
    addAndMakeVisible(loadModelButton);
    clearModelButton.onClick = [this] { processor.getEngine().models().requestClear(); };
    addAndMakeVisible(clearModelButton);

    modelStatusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modelStatusLabel);

    qualityCaption.setText("Quality", juce::dontSendNotification);
    qualityCaption.setJustificationType(juce::Justification::centredLeft);
    qualityCaption.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(qualityCaption);

    qualitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    qualitySlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    qualitySlider.setTooltip("Trades model size for CPU on slimmable models. "
                             "Full right = the complete model.");
    addAndMakeVisible(qualitySlider);
    qualityAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::slim.getParamID(), qualitySlider);

    // --- CAB / IR ---
    loadIrButton.onClick = [this] { chooseIr(); };
    addAndMakeVisible(loadIrButton);
    clearIrButton.onClick = [this] { processor.clearIr(); };
    addAndMakeVisible(clearIrButton);

    irToggleAttachment = std::make_unique<ButtonAttachment>(
        state, state::param_ids::irEnabled.getParamID(), irToggle);
    addAndMakeVisible(irToggle);

    irStatusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(irStatusLabel);

    attachCombo(stereoIrBox, state::param_ids::stereoIrMode, stereoIrAttachment);
    stereoIrBox.setTooltip("How a 2-channel IR is used when processing in stereo: "
                           "one channel per side, or collapse to mono and spread.");

    // --- OUTPUT ---
    setUpKnob(outputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::outputGain.getParamID(), outputSlider);
    attachCombo(outputModeBox, state::param_ids::outputMode, outputModeAttachment);
    outputModeBox.setTooltip("Normalized level-matches models that carry loudness "
                             "metadata; Raw leaves levels as captured.");

    setResizable(true, true);
    setResizeLimits(560, 300, 0x3fffffff, 0x3fffffff);
    setSize(720, 340);

    startTimerHz(4);
    timerCallback();
}

Editor::~Editor() = default;

void Editor::timerCallback()
{
    const auto info = processor.getEngine().models().info();

    if (!info.error.empty())
    {
        modelStatusLabel.setColour(juce::Label::textColourId, kError);
        modelStatusLabel.setText("Failed: " + juce::String(info.error),
                                 juce::dontSendNotification);
    }
    else if (info.loaded)
    {
        modelStatusLabel.setColour(juce::Label::textColourId, kText);
        modelStatusLabel.setText(juce::File(info.path).getFileNameWithoutExtension(),
                                 juce::dontSendNotification);
    }
    else
    {
        modelStatusLabel.setColour(juce::Label::textColourId, kDim);
        modelStatusLabel.setText("No model", juce::dontSendNotification);
    }
    clearModelButton.setEnabled(info.loaded);

    // Quality only means something for slimmable models.
    qualitySlider.setEnabled(info.slimmable);
    qualityCaption.setText(info.loaded && !info.slimmable ? "Quality (n/a for this model)"
                                                          : "Quality",
                           juce::dontSendNotification);

    if (processor.isIrLoaded())
    {
        irStatusLabel.setColour(juce::Label::textColourId, kText);
        irStatusLabel.setText(juce::File(processor.getIrPath()).getFileNameWithoutExtension(),
                              juce::dontSendNotification);
    }
    else
    {
        irStatusLabel.setColour(juce::Label::textColourId, kDim);
        irStatusLabel.setText("No IR", juce::dontSendNotification);
    }
    clearIrButton.setEnabled(processor.isIrLoaded());

    // The stereo-IR policy only exists for a 2ch IR under stereo processing;
    // anywhere else it's noise — hide it.
    const bool policyRelevant = processor.isStereoIrPolicyRelevant();
    if (stereoIrBox.isVisible() != policyRelevant)
    {
        stereoIrBox.setVisible(policyRelevant);
        resized();
    }

    topologyLabel.setText(processor.topologyDescription(), juce::dontSendNotification);
}

void Editor::chooseModel()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load NAM model", juce::File{}, "*.nam");
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

    for (const auto& section : sections)
    {
        if (section.frame.isEmpty())
            continue;
        g.setColour(kPanel);
        g.fillRoundedRectangle(section.frame.toFloat(), 6.0f);
        g.setColour(kFrame);
        g.drawRoundedRectangle(section.frame.toFloat(), 6.0f, 1.0f);
        g.setColour(kAccent);
        g.setFont(juce::FontOptions{12.0f}.withStyle("Bold"));
        g.drawText(section.title, section.frame.withHeight(22).reduced(10, 0),
                   juce::Justification::centredLeft);
    }
}

void Editor::resized()
{
    auto area = getLocalBounds().reduced(14);

    // Utility bar along the bottom, visually separate from the chain.
    auto utility = area.removeFromBottom(26);
    if (settingsButton.isVisible())
    {
        settingsButton.setBounds(utility.removeFromRight(110));
        utility.removeFromRight(10);
    }
    utility.removeFromLeft(10);
    topologyLabel.setBounds(utility);
    area.removeFromBottom(10);

    // Signal chain: four sections, left to right.
    const int gap = 10;
    const int knobSectionWidth = juce::jmax(120, area.getWidth() / 6);
    auto inputArea = area.removeFromLeft(knobSectionWidth);
    area.removeFromLeft(gap);
    auto outputArea = area.removeFromRight(knobSectionWidth);
    area.removeFromRight(gap);
    const int half = (area.getWidth() - gap) / 2;
    auto ampArea = area.removeFromLeft(half);
    area.removeFromLeft(gap);
    auto irArea = area;

    sections[0] = {inputArea, "INPUT"};
    sections[1] = {ampArea, "AMP"};
    sections[2] = {irArea, "CAB / IR"};
    sections[3] = {outputArea, "OUTPUT"};

    const int header = 24;

    // INPUT: gain knob, chain width below it (width is decided where
    // signal enters the chain).
    {
        auto r = inputArea.withTrimmedTop(header).reduced(8);
        channelsBox.setBounds(r.removeFromBottom(24));
        r.removeFromBottom(6);
        inputSlider.setBounds(r);
    }

    // AMP: buttons row, name, quality.
    {
        auto r = ampArea.withTrimmedTop(header).reduced(10);
        auto buttons = r.removeFromTop(26);
        loadModelButton.setBounds(buttons.removeFromLeft(84));
        buttons.removeFromLeft(6);
        clearModelButton.setBounds(buttons.removeFromLeft(64));
        r.removeFromTop(6);
        modelStatusLabel.setBounds(r.removeFromTop(24));
        auto quality = r.removeFromBottom(24);
        qualityCaption.setBounds(quality.removeFromLeft(juce::jmin(170, quality.getWidth() / 2)));
        qualitySlider.setBounds(quality);
    }

    // CAB / IR: buttons + toggle row, name, policy (when visible).
    {
        auto r = irArea.withTrimmedTop(header).reduced(10);
        auto buttons = r.removeFromTop(26);
        loadIrButton.setBounds(buttons.removeFromLeft(84));
        buttons.removeFromLeft(6);
        clearIrButton.setBounds(buttons.removeFromLeft(64));
        buttons.removeFromLeft(10);
        irToggle.setBounds(buttons);
        r.removeFromTop(6);
        irStatusLabel.setBounds(r.removeFromTop(24));
        if (stereoIrBox.isVisible())
            stereoIrBox.setBounds(r.removeFromBottom(24).removeFromLeft(150));
    }

    // OUTPUT: knob + mode.
    {
        auto r = outputArea.withTrimmedTop(header).reduced(8);
        outputModeBox.setBounds(r.removeFromBottom(24));
        r.removeFromBottom(6);
        outputSlider.setBounds(r);
    }
}

} // namespace namrig
