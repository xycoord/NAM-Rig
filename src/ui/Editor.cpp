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
const juce::Colour kOk{0xff77c79b};

// Staging meter scale and target zone (peak dBFS, post-trim). The zone is
// where the HARDEST playing should peak, anchored to upstream's reference
// DI (Guitar DI.wav: max peak -8.6 dBFS, typical playing peaks -16..-28).
// +/-5 dB is fine — Drive absorbs capture-level variance; the zone catches
// staging errors, not decibels.
constexpr float kMeterFloorDb = -48.0f;
constexpr float kTargetLowDb = -15.0f;
constexpr float kTargetHighDb = -6.0f;
} // namespace

// ---- StagingMeter -----------------------------------------------------------

void Editor::StagingMeter::setLevel(const float peakLinear)
{
    const float db = peakLinear > 1.0e-5f ? 20.0f * std::log10(peakLinear) : kMeterFloorDb;
    // Fast attack, slow decay.
    levelDb = juce::jmax(db, levelDb - 2.5f);
    clipped = peakLinear >= 1.0f || (clipped && levelDb > kMeterFloorDb + 6.0f);
    repaint();
}

void Editor::StagingMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(r, 3.0f);

    auto yFor = [&](float db) {
        const float t = juce::jlimit(0.0f, 1.0f, (db - kMeterFloorDb) / (0.0f - kMeterFloorDb));
        return r.getBottom() - t * r.getHeight();
    };

    // Target zone.
    g.setColour(kOk.withAlpha(0.25f));
    g.fillRect(juce::Rectangle<float>{r.getX(), yFor(kTargetHighDb), r.getWidth(),
                                      yFor(kTargetLowDb) - yFor(kTargetHighDb)});

    // Level bar: green in the zone, accent below, red above.
    const bool inZone = levelDb >= kTargetLowDb && levelDb <= kTargetHighDb;
    const bool hot = levelDb > kTargetHighDb;
    g.setColour(hot ? kError : (inZone ? kOk : kAccent.withAlpha(0.8f)));
    const float top = yFor(levelDb);
    g.fillRect(juce::Rectangle<float>{r.getX() + 2.0f, top, r.getWidth() - 4.0f,
                                      juce::jmax(0.0f, r.getBottom() - top - 2.0f)});

    if (clipped)
    {
        g.setColour(kError);
        g.fillRect(getLocalBounds().toFloat().removeFromTop(4.0f));
    }
}

// ---- Editor -----------------------------------------------------------------

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
    auto caption = [this](juce::Label& label, const char* text,
                          juce::Justification just = juce::Justification::centred) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(just);
        label.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(label);
    };
    auto knob = [this](juce::Slider& slider) {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
        slider.setTextValueSuffix(" dB");
        addAndMakeVisible(slider);
    };

    // --- utility bar ---
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
    addAndMakeVisible(meter);

    knob(trimSlider);
    trimSlider.setTooltip("Input staging gain. Place your hardest playing in the "
                          "meter's target zone once per rig; leave the rest to Drive.");
    trimAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::trim.getParamID(), trimSlider);
    caption(trimCaption, "Trim");

    attachCombo(channelsBox, state::param_ids::channels, channelsAttachment);
    channelsBox.setTooltip("Processing width. Auto follows the input bus in a DAW "
                           "and stays mono in the standalone.");
    caption(channelsCaption, "Channels", juce::Justification::centredLeft);

    // --- AMP ---
    loadModelButton.onClick = [this] { chooseModel(); };
    addAndMakeVisible(loadModelButton);
    clearModelButton.onClick = [this] { processor.getEngine().models().requestClear(); };
    addAndMakeVisible(clearModelButton);

    modelStatusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modelStatusLabel);

    knob(driveSlider);
    driveSlider.setTooltip("How hard the model is driven. The output is compensated "
                           "by the model's measured response, so loudness stays "
                           "close to constant.");
    driveAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::drive.getParamID(), driveSlider);
    caption(driveCaption, "Drive");

    caption(qualityCaption, "Quality", juce::Justification::centredLeft);
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
    attachCombo(outputModeBox, state::param_ids::outputMode, outputModeAttachment);
    outputModeBox.setTooltip("Normalized level-matches models that carry loudness "
                             "metadata; Raw leaves levels as captured.");

    normCaption.setJustificationType(juce::Justification::centredLeft);
    normCaption.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(normCaption);

    setResizable(true, true);
    setResizeLimits(640, 320, 0x3fffffff, 0x3fffffff);
    setSize(760, 360);

    startTimerHz(15); // meter pace; labels ride along
    timerCallback();
}

Editor::~Editor() = default;

void Editor::timerCallback()
{
    meter.setLevel(processor.consumeInputPeak());

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

    const bool policyRelevant = processor.isStereoIrPolicyRelevant();
    if (stereoIrBox.isVisible() != policyRelevant)
    {
        stereoIrBox.setVisible(policyRelevant);
        resized();
    }

    // Normalized-mode status: the applied offset, or missing metadata
    // (no silent fallback).
    const bool normalized = outputModeBox.getSelectedItemIndex() == 1;
    if (normCaption.isVisible() != normalized)
    {
        normCaption.setVisible(normalized);
        resized();
    }
    if (normalized)
    {
        if (info.loaded && !info.hasLoudness)
        {
            normCaption.setColour(juce::Label::textColourId, kError);
            normCaption.setText("Level (model has no loudness data)",
                                juce::dontSendNotification);
        }
        else
        {
            normCaption.setColour(juce::Label::textColourId, kDim);
            normCaption.setText(info.loaded
                                    ? juce::String(processor.getNormalizationOffsetDb(), 1)
                                          + " dB applied"
                                    : juce::String{},
                                juce::dontSendNotification);
        }
    }

    topologyLabel.setText(processor.topologyDescription(), juce::dontSendNotification);
    channelsBox.changeItemText(
        1, processor.getResolvedLanes() == 2 ? "Auto (stereo)" : "Auto (mono)");
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

    auto utility = area.removeFromBottom(26);
    if (settingsButton.isVisible())
    {
        settingsButton.setBounds(utility.removeFromRight(110));
        utility.removeFromRight(10);
    }
    topologyLabel.setBounds(utility);
    area.removeFromBottom(10);

    const int gap = 10;
    const int sideWidth = juce::jmax(160, area.getWidth() / 5);
    auto inputArea = area.removeFromLeft(sideWidth);
    area.removeFromLeft(gap);
    auto outputArea = area.removeFromRight(sideWidth);
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

    // INPUT: meter strip on the left; trim + channels beside it.
    {
        auto r = inputArea.withTrimmedTop(header).reduced(8);
        meter.setBounds(r.removeFromLeft(20));
        r.removeFromLeft(8);
        channelsBox.setBounds(r.removeFromBottom(22));
        channelsCaption.setBounds(r.removeFromBottom(16));
        r.removeFromBottom(4);
        trimCaption.setBounds(r.removeFromTop(16));
        trimSlider.setBounds(r);
    }

    // AMP: model row, name, drive knob, quality.
    {
        auto r = ampArea.withTrimmedTop(header).reduced(10);
        auto buttons = r.removeFromTop(26);
        loadModelButton.setBounds(buttons.removeFromLeft(84));
        buttons.removeFromLeft(6);
        clearModelButton.setBounds(buttons.removeFromLeft(64));
        r.removeFromTop(4);
        modelStatusLabel.setBounds(r.removeFromTop(22));
        auto quality = r.removeFromBottom(22);
        qualityCaption.setBounds(quality.removeFromLeft(juce::jmin(170, quality.getWidth() / 2)));
        qualitySlider.setBounds(quality);
        r.removeFromBottom(4);
        driveCaption.setBounds(r.removeFromTop(16));
        driveSlider.setBounds(r);
    }

    // CAB / IR.
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

    // OUTPUT: mode at top; Normalized level below it.
    {
        auto r = outputArea.withTrimmedTop(header).reduced(8);
        outputModeBox.setBounds(r.removeFromTop(24));
        r.removeFromTop(6);
        if (normCaption.isVisible())
            normCaption.setBounds(r.removeFromTop(16));
    }
}

} // namespace namrig
