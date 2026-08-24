#include "Editor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginProcessor.h"
#include "state/Parameters.h"

namespace namrig
{

namespace
{
using namespace theme::colours;
const juce::Colour kBackground = background;
const juce::Colour kPanel = panel;
const juce::Colour kFrame = outline;
const juce::Colour kText = textPrimary;
const juce::Colour kError = error;
const juce::Colour kDim = textSecondary;
const juce::Colour kAccent = accent;
const juce::Colour kOk = ok;

// Staging meter scale and target zone (peak dBFS, post-trim). The zone is
// where the HARDEST playing should peak, anchored to upstream's reference
// DI (Guitar DI.wav: max peak -8.6 dBFS, typical playing peaks -16..-28).
// +/-5 dB is fine — Drive absorbs capture-level variance; the zone catches
// staging errors, not decibels.
constexpr float kMeterFloorDb = -48.0f;
constexpr float kTargetLowDb = -15.0f;
constexpr float kTargetHighDb = -6.0f;
} // namespace

// ---- TunerStrip -------------------------------------------------------------

void Editor::TunerStrip::setReading(const float freqHz, const float clarity)
{
    const auto now = juce::Time::getMillisecondCounter();

    if (freqHz > 0.0f && clarity > 0.85f)
    {
        const double midi = 69.0 + 12.0 * std::log2(static_cast<double>(freqHz) / 440.0);
        const int nearest = static_cast<int>(std::lround(midi));
        const auto cents = static_cast<float>((midi - nearest) * 100.0);

        if (nearest != noteIndex)
        {
            noteIndex = nearest;
            centsSmoothed = cents; // new note: no smoothing across the jump
        }
        else
            centsSmoothed += 0.4f * (cents - centsSmoothed);

        // In-tune with hysteresis: enter inside +/-5c, leave outside +/-8c.
        inTune = inTune ? std::abs(centsSmoothed) < 8.0f : std::abs(centsSmoothed) < 5.0f;
        lastGoodMs = static_cast<juce::int64>(now);
        repaint();
    }
    else if (noteIndex >= 0
             && now - static_cast<juce::uint32>(lastGoodMs) > 700) // hold, then idle
    {
        noteIndex = -1;
        inTune = false;
        repaint();
    }
}

void Editor::TunerStrip::paint(juce::Graphics& g)
{
    // Frameless: the tuner is a readout, not a control group — it floats
    // on the window ground. The axis is always present (this IS a tuner,
    // signal or not); note and indicator appear only with a confident pitch.
    auto r = getLocalBounds().toFloat();
    const auto centreX = r.getCentreX();

    const float scaleW = r.getWidth() - 16.0f;
    const float scaleY = r.getBottom() - 8.0f;
    const float left = centreX - scaleW / 2.0f;
    g.setColour(kFrame);
    g.fillRect(juce::Rectangle<float>{left, scaleY, scaleW, 2.0f});
    for (const float c : {-50.0f, -25.0f, 0.0f, 25.0f, 50.0f})
    {
        const float x = centreX + (c / 50.0f) * (scaleW / 2.0f);
        const float h = c == 0.0f ? 8.0f : 5.0f;
        g.setColour(c == 0.0f ? kDim : kFrame);
        g.fillRect(juce::Rectangle<float>{x - 1.0f, scaleY + 1.0f - h, 2.0f, h});
    }

    if (noteIndex < 0)
        return;

    static const char* names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
    const auto* name = names[((noteIndex % 12) + 12) % 12];
    const int octave = noteIndex / 12 - 1;

    // Note (hero) + octave + signed cents.
    const auto noteColour = inTune ? kOk : kText;
    g.setColour(noteColour);
    g.setFont(juce::FontOptions{28.0f}.withStyle("Bold"));
    auto noteArea = getLocalBounds().withHeight(getHeight() - 16);
    g.drawText(name, noteArea, juce::Justification::centred);

    g.setFont(juce::FontOptions{13.0f});
    const auto noteWidth =
        juce::GlyphArrangement::getStringWidth(juce::Font{juce::FontOptions{30.0f}
                                                              .withStyle("Bold")},
                                               name);
    g.setColour(kDim);
    g.drawText(juce::String(octave),
               noteArea.withX(static_cast<int>(centreX + noteWidth / 2 + 2)).withWidth(24),
               juce::Justification::centredLeft);

    const auto centsText = juce::String{centsSmoothed > 0 ? "+" : ""}
                           + juce::String{static_cast<int>(std::lround(centsSmoothed))}
                           + juce::String{juce::CharPointer_UTF8{"\xc2\xa2"}};
    g.setColour(inTune ? kOk : kDim);
    g.drawText(centsText,
               noteArea.withX(static_cast<int>(centreX + noteWidth / 2 + 26)).withWidth(60),
               juce::Justification::centredLeft);

    // Bar from centre: right = sharp, left = flat. Green only in tune.
    const float clamped = juce::jlimit(-50.0f, 50.0f, centsSmoothed);
    const float endX = centreX + (clamped / 50.0f) * (scaleW / 2.0f);
    g.setColour(inTune ? kOk : kAccent);
    g.fillRect(juce::Rectangle<float>{juce::jmin(centreX, endX), scaleY - 4.0f,
                                      std::abs(endX - centreX), 6.0f});
    if (inTune)
    {
        g.fillEllipse(juce::Rectangle<float>{8.0f, 8.0f}.withCentre(
            {centreX, scaleY - 1.0f}));
    }
}

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
    g.setColour(inset);
    g.fillRoundedRectangle(r, 3.0f);

    auto yFor = [&](float db) {
        const float t = juce::jlimit(0.0f, 1.0f, (db - kMeterFloorDb) / (0.0f - kMeterFloorDb));
        return r.getBottom() - t * r.getHeight();
    };

    if (showZone)
    {
        g.setColour(kOk.withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float>{r.getX(), yFor(kTargetHighDb), r.getWidth(),
                                          yFor(kTargetLowDb) - yFor(kTargetHighDb)});
    }

    // Level bar: green in the zone, accent below, red above (zone meters);
    // plain accent with red-above-clip for the output strip.
    const bool inZone = showZone && levelDb >= kTargetLowDb && levelDb <= kTargetHighDb;
    const bool hot = showZone ? levelDb > kTargetHighDb : levelDb > -1.0f;
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
    setLookAndFeel(&lookAndFeel);
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
    auto knob = [this](KnobSlider& slider, double defaultValue) {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0); // value drawn in-knob
        slider.setTextValueSuffix(" dB");
        slider.setDoubleClickReturnValue(true, defaultValue);
        slider.setVelocityModeParameters(1.0, 1, 0.0, true,
                                         juce::ModifierKeys::shiftModifier); // shift = fine
        slider.onValueClick = [this, &slider] { openKnobValueEditor(slider); };
        addAndMakeVisible(slider);
    };

    knobValueEditor.setVisible(false);
    knobValueEditor.setJustification(juce::Justification::centred);
    knobValueEditor.setSelectAllWhenFocused(false);
    addChildComponent(knobValueEditor);

    // --- utility bar ---
    presetBox.setTextWhenNothingSelected("Presets");
    // Nothing stays formally "selected": the loaded name shows as
    // placeholder, so re-picking the same preset fires again (reload =
    // revert tweaks).
    presetBox.onChange = [this] {
        if (presetBox.getSelectedItemIndex() < 0)
            return;
        const auto name = presetBox.getText();
        presetBox.setSelectedId(0, juce::dontSendNotification);
        if (name.isNotEmpty() && processor.loadPreset(name))
            presetBox.setTextWhenNothingSelected(name);
    };
    addAndMakeVisible(presetBox);
    refreshPresetList();

    savePresetButton.onClick = [this] { promptSavePreset(); };
    addAndMakeVisible(savePresetButton);

    // Delete with an inline two-step confirm (no dialogs): arm, then confirm.
    deletePresetButton.onClick = [this] {
        const auto name = presetBox.getText();
        if (name.isEmpty())
            return;
        if (!deleteArmed)
        {
            deleteArmed = true;
            deletePresetButton.setButtonText("Sure?");
            juce::Timer::callAfterDelay(2500, [safe = juce::Component::SafePointer{this}] {
                if (safe != nullptr)
                {
                    safe->deleteArmed = false;
                    safe->deletePresetButton.setButtonText("Delete");
                }
            });
            return;
        }
        deleteArmed = false;
        deletePresetButton.setButtonText("Delete");
        processor.deletePreset(name);
        refreshPresetList();
    };
    addAndMakeVisible(deletePresetButton);

    // Inline name entry, shown in place of the dropdown while saving.
    presetNameEditor.setVisible(false);
    presetNameEditor.setSelectAllWhenFocused(false);
    presetNameEditor.onReturnKey = [this] {
        const auto name = presetNameEditor.getText().trim();
        if (name.isNotEmpty())
        {
            processor.savePreset(name);
            refreshPresetList();
        }
        presetNameEditor.setVisible(false);
        presetBox.setVisible(true);
    };
    auto dismiss = [this] {
        presetNameEditor.setVisible(false);
        presetBox.setVisible(true);
    };
    presetNameEditor.onEscapeKey = dismiss;
    presetNameEditor.onFocusLost = dismiss;
    addChildComponent(presetNameEditor);

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

    addAndMakeVisible(tunerStrip);
    processor.getEngine().tuner().setActive(true);

    // --- INPUT ---
    addAndMakeVisible(meter);
    addAndMakeVisible(outputMeter);

    // Trim is calibration, not performance: a fader beside the meter it
    // serves (channel-strip idiom), not a knob inviting play.
    trimSlider.setSliderStyle(juce::Slider::LinearVertical);
    trimSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    trimSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(trimSlider);
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
    modelRow.addTo(*this);
    modelRow.name.onClick = [this] { chooseModel(); };
    modelRow.prev.onClick = [this] { stepModel(-1); };
    modelRow.next.onClick = [this] { stepModel(1); };
    modelRow.clear.onClick = [this] { processor.getEngine().models().requestClear(); };
    modelRow.name.setTooltip("Click to pick a model; arrows step through its folder.");

    normStatusLabel.setJustificationType(juce::Justification::centred);
    normStatusLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(normStatusLabel);

    knob(tightSlider, 20.0);
    tightSlider.setTextValueSuffix({});
    tightSlider.setTooltip("Pre-gain high-pass (12 dB/oct). Tightens the low end "
                           "before distortion; at 20 Hz it's effectively off.");
    tightAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::tight.getParamID(), tightSlider);
    caption(tightCaption, "HPF");

    knob(toneSlider, 20000.0);
    toneSlider.getProperties().set("reverseFill", true);
    toneSlider.setTextValueSuffix({});
    toneSlider.setTooltip("Pre-gain low-pass, 6 dB/oct — like rolling off the "
                          "guitar's tone pot. Fully open = out of the path.");
    toneAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::tone.getParamID(), toneSlider);
    caption(toneCaption, "LPF");

    knob(driveSlider, 0.0);
    driveSlider.setTooltip("How hard the model is driven. The output is compensated "
                           "by the model's measured response, so loudness stays "
                           "close to constant.");
    driveAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::drive.getParamID(), driveSlider);
    caption(driveCaption, "Drive");

    caption(qualityCaption, "Quality", juce::Justification::centredLeft);
    qualityBox.setTooltip("This model's discrete quality levels: lower = fewer "
                          "network channels = less CPU.");
    qualityBox.onChange = [this] {
        const int idx = qualityBox.getSelectedItemIndex();
        if (idx >= 0 && idx < static_cast<int>(qualityLevelValues.size()))
            if (auto* param = processor.getState().getParameter(
                    state::param_ids::slim.getParamID()))
                param->setValueNotifyingHost(param->convertTo0to1(
                    static_cast<float>(qualityLevelValues[static_cast<size_t>(idx)])));
    };
    addAndMakeVisible(qualityBox);
    qualityCaption.setVisible(false);
    qualityBox.setVisible(false);

    // --- CAB / IR ---
    addAndMakeVisible(ampPower);
    ampPower.setTooltip("Amp section on/off (filters, drive, model)");
    ampPowerAttachment = std::make_unique<ButtonAttachment>(
        state, state::param_ids::ampEnabled.getParamID(), ampPower);
    ampPower.onStateChange = [this] { ampDim.setVisible(!ampPower.getToggleState()); };

    addAndMakeVisible(irPower);
    irPower.setTooltip("IR on/off (stays loaded while bypassed)");
    irPowerAttachment = std::make_unique<ButtonAttachment>(
        state, state::param_ids::irEnabled.getParamID(), irPower);
    irPower.onStateChange = [this] { irDim.setVisible(!irPower.getToggleState()); };

    addAndMakeVisible(verbPower);
    verbPower.setTooltip("Reverb on/off (the tail rings out on bypass)");
    verbPowerAttachment = std::make_unique<ButtonAttachment>(
        state, state::param_ids::verbEnabled.getParamID(), verbPower);
    verbPower.onStateChange = [this] { verbDim.setVisible(!verbPower.getToggleState()); };

    addChildComponent(ampDim);
    addChildComponent(irDim);
    addChildComponent(verbDim);

    irRow.addTo(*this);
    irRow.name.onClick = [this] { chooseIr(); };
    irRow.prev.onClick = [this] { stepIr(-1); };
    irRow.next.onClick = [this] { stepIr(1); };
    irRow.clear.onClick = [this] { processor.clearIr(); };
    irRow.name.setTooltip("Click to pick an IR; arrows step through its folder.");

    verbRow.addTo(*this);
    verbRow.name.onClick = [this] { chooseVerb(); };
    verbRow.prev.onClick = [this] { stepVerb(-1); };
    verbRow.next.onClick = [this] { stepVerb(1); };
    verbRow.clear.onClick = [this] { processor.clearVerbIr(); };
    verbRow.name.setTooltip("Click to pick a reverb IR; arrows step through its folder.");

    knob(predelaySlider, 0.0);
    predelaySlider.setTextValueSuffix({});
    predelaySlider.setTooltip("Delay before the reverb starts: separates the dry "
                              "attack from the room.");
    predelayAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::verbPredelay.getParamID(), predelaySlider);
    caption(predelayCaption, "Pre-delay");

    knob(verbHpfSlider, 20.0);
    verbHpfSlider.setTextValueSuffix({});
    verbHpfSlider.setTooltip("Filters lows out of the reverb send (the Abbey Road "
                             "trick, ~600 Hz): keeps the room out of the mud.");
    verbHpfAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::verbHpf.getParamID(), verbHpfSlider);
    caption(verbHpfCaption, "HPF");

    knob(verbLpfSlider, 20000.0);
    verbLpfSlider.getProperties().set("reverseFill", true);
    verbLpfSlider.setTextValueSuffix({});
    verbLpfSlider.setTooltip("Filters highs out of the reverb send (~10 kHz for the "
                             "classic dark chamber): no fizzy tails.");
    verbLpfAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::verbLpf.getParamID(), verbLpfSlider);
    caption(verbLpfCaption, "LPF");

    knob(sendSlider, -20.0);
    sendSlider.setTooltip("Reverb send level. At the floor the send is off; "
                          "with no reverb IR loaded nothing is sent at all.");
    sendAttachment = std::make_unique<SliderAttachment>(
        state, state::param_ids::verbSend.getParamID(), sendSlider);
    caption(sendCaption, "Send");

    attachCombo(stereoIrBox, state::param_ids::stereoIrMode, stereoIrAttachment);
    stereoIrBox.setTooltip("How a 2-channel IR is used when processing in stereo: "
                           "one channel per side, or collapse to mono and spread.");

    // Hear every descendant click so inline editors blur when the user
    // clicks anywhere outside them (non-focusable targets never trigger
    // onFocusLost).
    addMouseListener(this, true);

    setResizable(true, true);
    setResizeLimits(640, 380, 0x3fffffff, 0x3fffffff);
    setSize(760, 430);

    startTimerHz(15); // meter pace; labels ride along
    timerCallback();
}

Editor::~Editor()
{
    processor.getEngine().tuner().setActive(false);
    setLookAndFeel(nullptr);
}

void Editor::timerCallback()
{
    meter.setLevel(processor.consumeInputPeak());
    outputMeter.setLevel(processor.consumeOutputPeak());
    tunerStrip.setReading(processor.getEngine().tuner().frequencyHz(),
                          processor.getEngine().tuner().clarity());

    const auto info = processor.getEngine().models().info();

    modelRow.name.setButtonText(info.loaded
                                    ? juce::File{juce::String{info.path}}
                                          .getFileNameWithoutExtension()
                                    : juce::String{"Select model..."});
    modelRow.clear.setEnabled(info.loaded);

    // Quality: a row of the model's real discrete levels; absent entirely
    // when the model isn't slimmable (silence is health).
    const bool showQuality = info.loaded && info.slimmable;
    if (qualityBox.isVisible() != showQuality)
    {
        qualityCaption.setVisible(showQuality);
        qualityBox.setVisible(showQuality);
        resized();
    }
    if (showQuality && info.qualityBreakpoints != shownBreakpoints)
    {
        shownBreakpoints = info.qualityBreakpoints;
        rebuildQualityLevels(shownBreakpoints);
    }
    if (showQuality)
        syncQualitySelection();

    verbRow.name.setButtonText(processor.isVerbLoaded()
                                   ? juce::File{processor.getVerbPath()}
                                         .getFileNameWithoutExtension()
                                   : juce::String{"Select reverb IR..."});
    verbRow.clear.setEnabled(processor.isVerbLoaded());
    for (auto* sl : {&sendSlider, &predelaySlider, &verbHpfSlider, &verbLpfSlider})
        sl->setEnabled(processor.isVerbLoaded());

    irRow.name.setButtonText(processor.isIrLoaded()
                                 ? juce::File{processor.getIrPath()}
                                       .getFileNameWithoutExtension()
                                 : juce::String{"Select IR..."});
    irRow.clear.setEnabled(processor.isIrLoaded());

    const bool policyRelevant = processor.isStereoIrPolicyRelevant();
    if (stereoIrBox.isVisible() != policyRelevant)
    {
        stereoIrBox.setVisible(policyRelevant);
        resized();
    }

    // Level management working correctly is invisible; only the case that
    // needs the user's attention gets a line (never a silent fallback).
    // Load failures take priority over the metadata note.
    if (!info.error.empty())
    {
        normStatusLabel.setColour(juce::Label::textColourId, kError);
        normStatusLabel.setText("Load failed: " + juce::String(info.error),
                                juce::dontSendNotification);
    }
    else if (info.loaded && !info.hasLoudness)
    {
        normStatusLabel.setColour(juce::Label::textColourId, kError);
        normStatusLabel.setText("volume not managed (model lacks loudness data)",
                                juce::dontSendNotification);
    }
    else
        normStatusLabel.setText({}, juce::dontSendNotification);

    // Diverged-from-preset marker.
    {
        const auto current = processor.getCurrentPresetName();
        const auto shown = current.isEmpty()
                               ? juce::String{"Presets"}
                               : current + (processor.isPresetDirty() ? " *" : "");
        if (presetBox.getTextWhenNothingSelected() != shown)
            presetBox.setTextWhenNothingSelected(shown);
    }

    topologyLabel.setText(processor.topologyDescription(), juce::dontSendNotification);
    channelsBox.changeItemText(
        1, processor.getResolvedLanes() == 2 ? "Auto (stereo)" : "Auto (mono)");
}

static juce::Array<juce::File> listSorted(const juce::File& dir, const juce::String& patterns)
{
    auto files = dir.findChildFiles(juce::File::findFiles, false, patterns);
    files.sort();
    return files;
}

static juce::File stepIn(const juce::Array<juce::File>& files, const juce::File& current,
                         const int delta)
{
    if (files.isEmpty())
        return {};
    const int idx = files.indexOf(current);
    if (idx < 0)
        return files[0];
    return files[(idx + delta + files.size()) % files.size()];
}

void Editor::stepModel(const int delta)
{
    const auto info = processor.getEngine().models().info();
    const juce::File current{juce::String{info.path}};
    const auto dir = info.loaded ? current.getParentDirectory()
                                 : processor.getLibrary().modelsDir();
    const auto target = stepIn(listSorted(dir, "*.nam"), current, delta);
    if (target.existsAsFile())
        processor.getEngine().models().requestLoad(target.getFullPathName().toStdString());
}

void Editor::stepIr(const int delta)
{
    const juce::File current{processor.getIrPath()};
    const auto dir = processor.isIrLoaded() ? current.getParentDirectory()
                                            : processor.getLibrary().irRoot();
    const auto target =
        stepIn(listSorted(dir, "*.wav;*.aif;*.aiff;*.flac"), current, delta);
    if (target.existsAsFile())
        processor.loadIr(target);
}

void Editor::openKnobValueEditor(KnobSlider& slider)
{
    const auto centre = getLocalPoint(&slider, slider.getLocalBounds().getCentre());
    knobValueEditor.setBounds(
        juce::Rectangle<int>{74, 20}.withCentre(centre));
    knobValueEditor.setText(slider.getTextFromValue(slider.getValue()));
    knobValueEditor.onReturnKey = [this, &slider] {
        slider.setValue(slider.getValueFromText(knobValueEditor.getText()),
                        juce::sendNotificationSync);
        knobValueEditor.setVisible(false);
    };
    auto dismiss = [this] { knobValueEditor.setVisible(false); };
    knobValueEditor.onEscapeKey = dismiss;
    knobValueEditor.onFocusLost = dismiss;
    knobValueEditor.setVisible(true);
    knobValueEditor.toFront(true);
    knobValueEditor.grabKeyboardFocus();
    knobValueEditor.moveCaretToEnd();
}

void Editor::rebuildQualityLevels(const std::vector<double>& breakpoints)
{
    // Segments between breakpoints are the model's real states; represent
    // each by its midpoint so ratio_to_channels lands inside it.
    std::vector<double> bounds{0.0};
    bounds.insert(bounds.end(), breakpoints.begin(), breakpoints.end());
    bounds.push_back(1.0);

    qualityLevelValues.clear();
    qualityBox.clear(juce::dontSendNotification);
    const int numLevels = static_cast<int>(bounds.size()) - 1;
    for (int i = 0; i < numLevels; ++i)
    {
        qualityLevelValues.push_back(0.5 * (bounds[static_cast<size_t>(i)]
                                            + bounds[static_cast<size_t>(i) + 1]));
        const auto label = i == numLevels - 1
                               ? juce::String{"Full"}
                               : juce::String(i + 1) + " / " + juce::String(numLevels);
        qualityBox.addItem(label, i + 1);
    }
}

void Editor::syncQualitySelection()
{
    if (auto* raw = processor.getState().getRawParameterValue(
            state::param_ids::slim.getParamID()))
    {
        const double v = raw->load();
        int idx = 0;
        for (size_t i = 0; i < qualityLevelValues.size(); ++i)
        {
            // Segment i spans [bounds_i, bounds_i+1); pick by nearest rep.
            if (std::abs(qualityLevelValues[i] - v)
                < std::abs(qualityLevelValues[static_cast<size_t>(idx)] - v))
                idx = static_cast<int>(i);
        }
        if (qualityBox.getSelectedItemIndex() != idx)
            qualityBox.setSelectedItemIndex(idx, juce::dontSendNotification);
    }
}

void Editor::refreshPresetList()
{
    presetBox.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& name : processor.getLibrary().listPresets())
        presetBox.addItem(name, id++);
    presetBox.setSelectedId(0, juce::dontSendNotification);
    const auto current = processor.getCurrentPresetName();
    presetBox.setTextWhenNothingSelected(current.isNotEmpty() ? current
                                                              : juce::String{"Presets"});
}

void Editor::promptSavePreset()
{
    // Swap the dropdown for a text field: Enter saves, Esc cancels.
    presetNameEditor.setBounds(presetBox.getBounds());
    presetNameEditor.setText(processor.getCurrentPresetName());
    presetBox.setVisible(false);
    presetNameEditor.setVisible(true);
    presetNameEditor.grabKeyboardFocus();
    presetNameEditor.moveCaretToEnd();
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

void Editor::stepVerb(const int delta)
{
    const juce::File current{processor.getVerbPath()};
    const auto dir = processor.isVerbLoaded() ? current.getParentDirectory()
                                              : processor.getLibrary().irRoot();
    const auto target =
        stepIn(listSorted(dir, "*.wav;*.aif;*.aiff;*.flac"), current, delta);
    if (target.existsAsFile())
        processor.loadVerbIr(target);
}

void Editor::chooseVerb()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load reverb impulse response",
                                                      juce::File{},
                                                      "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) {
                                 const auto file = fc.getResult();
                                 if (file.existsAsFile())
                                     processor.loadVerbIr(file);
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

void Editor::mouseDown(const juce::MouseEvent& e)
{
    auto outside = [&](juce::TextEditor& ed) {
        return ed.isVisible() && e.eventComponent != &ed
               && !ed.isParentOf(e.eventComponent);
    };
    if (outside(knobValueEditor))
        knobValueEditor.setVisible(false);
    if (outside(presetNameEditor))
    {
        presetNameEditor.setVisible(false);
        presetBox.setVisible(true);
    }
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
        g.setColour(kDim);
        g.setFont(juce::FontOptions{11.0f}.withStyle("Bold")); // header: quiet, not accent
        g.drawText(section.title, section.frame.withHeight(22).reduced(10, 0),
                   juce::Justification::centredLeft);
    }
}

void Editor::resized()
{
    auto area = getLocalBounds().reduced(14);

    tunerStrip.setBounds(area.removeFromTop(48));
    area.removeFromTop(6);

    auto utility = area.removeFromBottom(26);
    presetBox.setBounds(utility.removeFromLeft(180));
    utility.removeFromLeft(6);
    savePresetButton.setBounds(utility.removeFromLeft(70));
    utility.removeFromLeft(6);
    deletePresetButton.setBounds(utility.removeFromLeft(60));
    utility.removeFromLeft(10);
    if (settingsButton.isVisible())
    {
        settingsButton.setBounds(utility.removeFromRight(110));
        utility.removeFromRight(10);
    }
    topologyLabel.setBounds(utility);
    area.removeFromBottom(10);

    const int gap = 10;
    auto outArea = area.removeFromRight(40); // meter width matches INPUT's
    area.removeFromRight(gap);
    auto inputArea = area.removeFromLeft(100); // fixed: a channel strip, not a panel that grows
    area.removeFromLeft(gap);
    const int half = (area.getWidth() - gap) * 11 / 20; // AMP slightly wider
    auto ampArea = area.removeFromLeft(half);
    area.removeFromLeft(gap);
    auto rightCol = area;
    auto irArea = rightCol.removeFromTop((rightCol.getHeight() - gap) * 2 / 5);
    rightCol.removeFromTop(gap);
    auto verbArea = rightCol;

    sections[0] = {inputArea, "INPUT"};
    sections[1] = {ampArea, "AMP"};
    sections[2] = {irArea, "CAB / IR"};
    sections[3] = {verbArea, "REVERB"};
    sections[4] = {outArea, "OUT"};

    ampPower.setBounds(ampArea.getX() + ampArea.getWidth() - 26, ampArea.getY() + 3, 18, 18);
    irPower.setBounds(irArea.getX() + irArea.getWidth() - 26, irArea.getY() + 3, 18, 18);
    verbPower.setBounds(verbArea.getX() + verbArea.getWidth() - 26, verbArea.getY() + 3, 18,
                        18);
    ampDim.setBounds(ampArea.withTrimmedTop(22));
    irDim.setBounds(irArea.withTrimmedTop(22));
    verbDim.setBounds(verbArea.withTrimmedTop(22));
    ampDim.setVisible(!ampPower.getToggleState());
    irDim.setVisible(!irPower.getToggleState());
    verbDim.setVisible(!verbPower.getToggleState());
    ampDim.toFront(false);
    irDim.toFront(false);
    verbDim.toFront(false);

    const int header = 20;

    outputMeter.setBounds(outArea.withTrimmedTop(header).reduced(8, 5));

    // INPUT: slim meter fused to the trim fader, centred as one unit;
    // channels below.
    {
        // Mirror of OUT: full-height meter on the panel edge; trim column
        // beside it.
        auto r = inputArea.withTrimmedTop(header).reduced(8, 5);
        meter.setBounds(r.removeFromLeft(24)); // same width as OUT's meter
        r.removeFromLeft(4);
        trimCaption.setBounds(r.removeFromTop(16));
        trimSlider.setBounds(r);
    }

    // AMP: selector row, status line, drive knob, quality.
    {
        auto r = ampArea.withTrimmedTop(header).reduced(10, 6);
        modelRow.layout(r.removeFromTop(26));
        r.removeFromTop(4);
        normStatusLabel.setBounds(r.removeFromTop(16));
        {
            auto bottomRow = r.removeFromBottom(24);
            if (qualityBox.isVisible())
            {
                qualityCaption.setBounds(bottomRow.removeFromLeft(52));
                qualityBox.setBounds(bottomRow.removeFromLeft(110));
                bottomRow.removeFromLeft(14);
            }
            channelsCaption.setBounds(bottomRow.removeFromLeft(62));
            channelsBox.setBounds(bottomRow.removeFromLeft(118));
        }
        r.removeFromBottom(4);
        // Knob row in signal order: Tight, Tone, then Drive as the hero.
        // All three share the caption line and the value-box baseline; only
        // the knob diameters differ.
        // Knob + label as one tight unit, centred in its cell: the knob
        // fills what the cell allows, the label sits directly beneath it.
        auto place = [](juce::Rectangle<int> cell, juce::Label& cap, juce::Slider& sl) {
            const int labelH = 15;
            const int dia =
                juce::jmin(cell.getWidth(), cell.getHeight() - labelH) - 2;
            // Bias the unit toward the top of its cell (dead space reads
            // worse above content than below), and tuck the label under the
            // knob's VISUAL edge (the draw insets ~8px inside the bounds).
            const int top = cell.getY() + (cell.getHeight() - dia - labelH) / 3;
            const auto knobArea =
                juce::Rectangle<int>{dia, dia}.withCentre({cell.getCentreX(), top + dia / 2});
            sl.setBounds(knobArea);
            cap.setBounds(cell.getX(), knobArea.getBottom() - 7, cell.getWidth(), labelH);
        };
        // Filters stacked in one column left of Drive: pre-gain shaping as
        // a unit, the hero knob beside it.
        auto filterCol = r.removeFromLeft(juce::jmin(r.getWidth() / 3, 110));
        auto tightCell = filterCol.removeFromTop(filterCol.getHeight() / 2);
        place(tightCell.reduced(0, 2), tightCaption, tightSlider);
        place(filterCol.reduced(0, 2), toneCaption, toneSlider);
        place(r.reduced(6, 0), driveCaption, driveSlider);
    }

    // CAB / IR: selector row, then toggles.
    {
        auto r = irArea.withTrimmedTop(header).reduced(10, 6);
        irRow.layout(r.removeFromTop(26));
        r.removeFromTop(8);
        if (stereoIrBox.isVisible())
            stereoIrBox.setBounds(r.removeFromTop(24).removeFromLeft(140));
    }

    // REVERB: selector row + knob row (Pre-delay, HPF, LPF, Send).
    {
        auto r = verbArea.withTrimmedTop(header).reduced(10, 6);
        verbRow.layout(r.removeFromTop(26));
        r.removeFromTop(6);
        auto placeKnob = [](juce::Rectangle<int> cell, juce::Label& cap, juce::Slider& sl) {
            const int labelH = 15;
            const int dia = juce::jmin(cell.getWidth(), cell.getHeight() - labelH) - 2;
            if (dia < 20)
                return;
            const int top = cell.getY() + (cell.getHeight() - dia - labelH) / 3;
            const auto knobArea =
                juce::Rectangle<int>{dia, dia}.withCentre({cell.getCentreX(), top + dia / 2});
            sl.setBounds(knobArea);
            cap.setBounds(cell.getX(), knobArea.getBottom() - 7, cell.getWidth(), labelH);
        };
        const int cellW = r.getWidth() / 4;
        placeKnob(r.removeFromLeft(cellW), predelayCaption, predelaySlider);
        placeKnob(r.removeFromLeft(cellW), verbHpfCaption, verbHpfSlider);
        placeKnob(r.removeFromLeft(cellW), verbLpfCaption, verbLpfSlider);
        placeKnob(r, sendCaption, sendSlider);
    }

}

} // namespace namrig
