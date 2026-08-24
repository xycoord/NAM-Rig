#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "engine/Engine.h"
#include "state/Library.h"
#include "state/Parameters.h"

namespace namrig
{

// JUCE shell around engine::Engine. Owns the gains (SmoothedValues), lane
// building and channel topology, the IR stage, and the message-thread
// duties the engine can't do itself: latency reporting, output-mode
// normalization, Quality (slim) forwarding, and topology resolution — via a
// 10 Hz timer, never from the audio thread (CLAUDE.md rules 3 & 4).
//
// Channel design (docs/plan.md): processing width comes from the input bus
// (Auto; mono in the standalone) or the Channels override. Stereo = two
// instances of the same model. IR topology is inferred from the IR file's
// channel count: 1 = cab, 2 = mono->stereo or dual-mono (Stereo IR param),
// 4 = true stereo (LL,LR,RL,RR as two stereo convolutions).
class Processor final : public juce::AudioProcessor, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // don't hide the double overload
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NAM Rig"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // DC blocker decay: 10 cycles at 5 Hz, matching the old plugin's tail.
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }
    engine::Engine& getEngine() { return engine; }

    // IR management (message thread).
    void loadIr(const juce::File& file);
    void clearIr();
    juce::String getIrPath() const { return irPath; }
    bool isIrLoaded() const { return irLoaded.load(std::memory_order_relaxed); }

    // Reverb IR (parallel send, post-cab).
    void loadVerbIr(const juce::File& file);
    void clearVerbIr();
    juce::String getVerbPath() const { return verbPath; }
    bool isVerbLoaded() const { return verbLoaded.load(std::memory_order_relaxed); }

    // Presets (message thread). A preset is THE SOUND: model, IR, IR on/off,
    // Drive, Quality, stereo-IR policy (+ reserved per-model trim). Trim and
    // Channels are machine/context state and are never touched by presets.
    state::Library& getLibrary() { return library; }
    bool savePreset(const juce::String& name);
    bool loadPreset(const juce::String& name); // false = file/parse failure
    void deletePreset(const juce::String& name);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    // True when the current sound diverges from the loaded preset.
    bool isPresetDirty() const;

    // "stereo in -> 2x amp -> quad IR -> stereo out" for the UI status line.
    juce::String topologyDescription() const;

    // Post-trim input peak since last call (linear); for the staging meter.
    float consumeInputPeak() { return inputPeak.exchange(0.0f, std::memory_order_relaxed); }
    // Output peak since last call (linear).
    float consumeOutputPeak() { return outputPeak.exchange(0.0f, std::memory_order_relaxed); }

    // Offset currently applied by Normalized mode (dB), for UI display.
    float getNormalizationOffsetDb() const
    {
        return normalizationOffsetDb.load(std::memory_order_relaxed);
    }

    // Resolved processing width (1 or 2), for UI display.
    int getResolvedLanes() const { return procLanes.load(std::memory_order_relaxed); }

    // True only when the Stereo IR policy actually affects anything: a
    // 2-channel IR while the amp would otherwise process stereo.
    bool isStereoIrPolicyRelevant() const
    {
        if (irNumChannels.load(std::memory_order_relaxed) != 2)
            return false;
        const int mode = static_cast<int>(channelsParam->load());
        if (mode == 1) // forced mono
            return false;
        if (mode == 2) // forced stereo
            return true;
        return !juce::JUCEApplicationBase::isStandaloneApp() && busInputChannels >= 2;
    }

private:
    // Audio-thread view of the resolved topology.
    enum class IrTopology : int
    {
        none = 0,     // no IR (or bypassed-by-absence)
        simple,       // primary convolution at processing width
        monoToStereo, // widen mono lane, then primary (2ch IR)
        quad          // two stereo convolutions: [LL,LR] and [RL,RR]
    };

    void timerCallback() override; // message thread
    void resolveTopology();        // message thread
    void applyGainRamp(juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>& smoother,
                       int numFrames); // fills gainRamp

    juce::AudioProcessorValueTreeState state;
    engine::Engine engine;
    state::Library library;
    juce::String currentPresetName;

    // Snapshot of the sound at last preset load/save, for divergence checks.
    struct PresetSnapshot
    {
        bool valid = false;
        juce::String modelPath, irPath;
        float drive = 0, quality = 0, tight = 20, tone = 20000, verbSend = -20;
        juce::String verbPath;
        bool irEnabled = true, ampEnabled = true, verbEnabled = true;
        int stereoMode = 0;
    } presetSnapshot;
    void capturePresetSnapshot(const juce::String& pendingModelPath);

    void setParamFromPreset(const juce::ParameterID& id, float naturalValue);

    // Raw parameter pointers (atomic reads on the audio thread).
    std::atomic<float>* driveDb = nullptr;
    std::atomic<float>* trimDb = nullptr;
    std::atomic<float>* slimParam = nullptr;
    std::atomic<float>* irEnabledParam = nullptr;
    std::atomic<float>* ampEnabledParam = nullptr;
    std::atomic<float>* verbSendParam = nullptr;
    std::atomic<float>* verbEnabledParam = nullptr;
    std::atomic<float>* channelsParam = nullptr;
    std::atomic<float>* stereoIrModeParam = nullptr;
    std::atomic<float>* tightParam = nullptr;
    std::atomic<float>* toneParam = nullptr;

    std::atomic<float> normalizationOffsetDb{0.0f};

    // Resolved topology (message thread writes, audio thread reads).
    std::atomic<int> procLanes{1};
    std::atomic<int> irTopology{static_cast<int>(IrTopology::none)};

    // Rule: every gain is smoothed. Ramps are rendered once per chunk into
    // gainRamp so both lanes see identical values.
    // trimGain: input staging (pre-meter). driveGain: into the model.
    // outputGain: measured-rise compensation + normalization (no user volume
    // in Raw — raw means raw).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> trimGain, driveGain, outputGain,
        irMix, ampMix, verbSendGain;

    // Post-trim input peak for the staging meter (audio writes, UI consumes).
    std::atomic<float> inputPeak{0.0f};
    // Post-everything output peak (what leaves the plugin).
    std::atomic<float> outputPeak{0.0f};

    // Pre-amp filters (trim -> tight -> tone -> drive -> model). TPT
    // structures: correct near-Nyquist response, click-free cutoff sweeps.
    // Always processed 2-channel (constant shape); per-lane state.
    juce::dsp::StateVariableTPTFilter<float> tightFilter; // 12 dB/oct HP
    juce::dsp::FirstOrderTPTFilter<float> toneFilter;     // 6 dB/oct LP
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> tightHz, toneHz;

    // IR stage. convPrimary serves 1ch and 2ch IRs (and the LL/LR half of a
    // quad); convQuadB is the RL/RR half, processed only in quad topology.
    juce::dsp::Convolution convPrimary, convQuadB, convVerb;
    juce::AudioFormatManager irFormats;
    juce::String irPath;
    std::atomic<bool> irLoaded{false};
    std::atomic<int> irNumChannels{0};
    juce::String verbPath;
    std::atomic<bool> verbLoaded{false};
    std::atomic<int> verbNumChannels{0};

    // Preallocated lane workspaces (prepareToPlay).
    std::vector<float> lane0, lane1, dry0, dry1, ampDry0, ampDry1, quadB0, quadB1, verb0,
        verb1, gainRamp;
    int preparedBlockSize = 0;
    int busInputChannels = 2, busOutputChannels = 2; // cached for resolve

    float lastForwardedSlim = -1.0f; // timer-side cache

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace namrig
