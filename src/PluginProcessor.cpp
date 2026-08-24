#include "PluginProcessor.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "ui/Editor.h"

namespace namrig
{

namespace
{
// Normalized-mode target loudness. With the input staged to the meter's
// target zone, bypass loudness is pinned too, so the bypass-matching target
// is a CONSTANT, not a user trim: zone-staged hard peaks ~-9 dBFS minus a
// typical guitar crest factor (~13 dB) puts dry loudness near -22 dBFS.
// One number to adjust if by-ear verification disagrees.
constexpr float kNormTargetDb = -22.0f;

// Channels parameter values.
enum
{
    kChannelsAuto = 0,
    kChannelsMono = 1,
    kChannelsStereo = 2
};

// Stereo IR parameter values.
enum
{
    kStereoIrDualMono = 0,
    kStereoIrMonoToStereo = 1
};
} // namespace

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "state", state::createParameterLayout())
{
    driveDb = state.getRawParameterValue(state::param_ids::drive.getParamID());
    trimDb = state.getRawParameterValue(state::param_ids::trim.getParamID());
    slimParam = state.getRawParameterValue(state::param_ids::slim.getParamID());
    irEnabledParam = state.getRawParameterValue(state::param_ids::irEnabled.getParamID());
    ampEnabledParam = state.getRawParameterValue(state::param_ids::ampEnabled.getParamID());
    verbSendParam = state.getRawParameterValue(state::param_ids::verbSend.getParamID());
    channelsParam = state.getRawParameterValue(state::param_ids::channels.getParamID());
    stereoIrModeParam = state.getRawParameterValue(state::param_ids::stereoIrMode.getParamID());
    tightParam = state.getRawParameterValue(state::param_ids::tight.getParamID());
    toneParam = state.getRawParameterValue(state::param_ids::tone.getParamID());

    irFormats.registerBasicFormats();

    // Development convenience until the file browser exists (milestone 4).
    if (const char* modelPath = std::getenv("NAMRIG_MODEL"))
        engine.models().requestLoad(std::filesystem::path{modelPath});

    startTimerHz(10);
}

Processor::~Processor()
{
    stopTimer();
}

void Processor::prepareToPlay(const double sampleRate, const int maximumExpectedSamplesPerBlock)
{
    preparedBlockSize = maximumExpectedSamplesPerBlock;
    const auto sz = static_cast<size_t>(preparedBlockSize);
    for (auto* v : {&lane0, &lane1, &dry0, &dry1, &ampDry0, &ampDry1, &quadB0, &quadB1,
                    &verb0, &verb1, &gainRamp})
        v->assign(sz, 0.0f);

    busInputChannels = getTotalNumInputChannels();
    busOutputChannels = getTotalNumOutputChannels();

    engine.prepare(sampleRate, preparedBlockSize);
    resolveTopology();

    // Both convolutions are prepared 2-channel and always processed at that
    // shape — no reallocation when the topology changes.
    const juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(preparedBlockSize), 2};
    convPrimary.prepare(spec);
    convQuadB.prepare(spec);
    convVerb.prepare(spec);

    tightFilter.prepare(spec);
    tightFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    tightFilter.setResonance(0.7071f); // Butterworth
    toneFilter.prepare(spec);
    toneFilter.setType(juce::dsp::FirstOrderTPTFilterType::lowpass);
    for (auto* sm : {&tightHz, &toneHz})
        sm->reset(sampleRate / preparedBlockSize, 0.05); // per-chunk update rate
    tightHz.setCurrentAndTargetValue(tightParam->load());
    toneHz.setCurrentAndTargetValue(toneParam->load());
    tightFilter.setCutoffFrequency(tightParam->load());
    toneFilter.setCutoffFrequency(toneParam->load());

    const double rampSeconds = 0.02;
    for (auto* s : {&trimGain, &driveGain, &outputGain, &irMix, &ampMix, &verbSendGain})
        s->reset(sampleRate, rampSeconds);
    ampMix.setCurrentAndTargetValue(ampEnabledParam->load() >= 0.5f ? 1.0f : 0.0f);
    const float drive0 = driveDb->load();
    trimGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(trimDb->load()));
    driveGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(drive0));
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        ampEnabledParam->load() >= 0.5f
            ? -engine.models().measuredRiseDb(drive0)
                  + normalizationOffsetDb.load(std::memory_order_relaxed)
            : 0.0f));
    irMix.setCurrentAndTargetValue(irEnabledParam->load() >= 0.5f ? 1.0f : 0.0f);
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void Processor::applyGainRamp(
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>& smoother, const int numFrames)
{
    for (int i = 0; i < numFrames; ++i)
        gainRamp[static_cast<size_t>(i)] = smoother.getNextValue();
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalFrames = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int numIn = juce::jmin(getTotalNumInputChannels(), numChannels);
    const int numOut = juce::jmin(getTotalNumOutputChannels(), numChannels);

    const int lanes = procLanes.load(std::memory_order_relaxed);
    const auto topo = static_cast<IrTopology>(irTopology.load(std::memory_order_relaxed));
    const bool haveIr = irLoaded.load(std::memory_order_relaxed);

    // Gain staging: Trim places the incoming signal (metered against a
    // target zone); Drive pushes the model with the MEASURED output rise
    // subtracted after it; Normalized adds its offset at the output.
    const float drive = driveDb->load();
    const bool ampOn = ampEnabledParam->load() >= 0.5f;
    trimGain.setTargetValue(juce::Decibels::decibelsToGain(trimDb->load()));
    driveGain.setTargetValue(juce::Decibels::decibelsToGain(drive));
    ampMix.setTargetValue(ampOn ? 1.0f : 0.0f);
    // Amp off = dry: the measured-rise/normalization gains disengage too.
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(
        ampOn ? -engine.models().measuredRiseDb(drive)
                    + normalizationOffsetDb.load(std::memory_order_relaxed)
              : 0.0f));
    irMix.setTargetValue((irEnabledParam->load() >= 0.5f && haveIr) ? 1.0f : 0.0f);
    tightHz.setTargetValue(tightParam->load());
    toneHz.setTargetValue(toneParam->load());
    // Send floor = off; no verb IR = stage skipped entirely.
    const float sendDb = verbSendParam->load();
    verbSendGain.setTargetValue(sendDb <= -59.5f ? 0.0f
                                                 : juce::Decibels::decibelsToGain(sendDb));

    for (int offset = 0; offset < totalFrames; offset += preparedBlockSize)
    {
        const int n = juce::jmin(totalFrames - offset, preparedBlockSize);
        const auto bytes = sizeof(float) * static_cast<size_t>(n);

        // ---- build lanes ---------------------------------------------------
        if (lanes == 1)
        {
            const float scale = numIn > 0 ? 1.0f / static_cast<float>(numIn) : 0.0f;
            for (int s = 0; s < n; ++s)
            {
                float mono = 0.0f;
                for (int c = 0; c < numIn; ++c)
                    mono += buffer.getReadPointer(c)[offset + s];
                lane0[static_cast<size_t>(s)] = mono * scale;
            }
        }
        else
        {
            std::memcpy(lane0.data(), buffer.getReadPointer(0) + offset, bytes);
            const int rightSource = numIn > 1 ? 1 : 0;
            std::memcpy(lane1.data(), buffer.getReadPointer(rightSource) + offset, bytes);
        }

        // ---- input trim + staging meter tap --------------------------------
        applyGainRamp(trimGain, n);
        float chunkPeak = 0.0f;
        for (int s = 0; s < n; ++s)
        {
            lane0[static_cast<size_t>(s)] *= gainRamp[static_cast<size_t>(s)];
            chunkPeak = juce::jmax(chunkPeak, std::abs(lane0[static_cast<size_t>(s)]));
        }
        if (lanes == 2)
            for (int s = 0; s < n; ++s)
            {
                lane1[static_cast<size_t>(s)] *= gainRamp[static_cast<size_t>(s)];
                chunkPeak = juce::jmax(chunkPeak, std::abs(lane1[static_cast<size_t>(s)]));
            }
        // Single audio-thread writer: load/max/store is race-free.
        inputPeak.store(juce::jmax(inputPeak.load(std::memory_order_relaxed), chunkPeak),
                        std::memory_order_relaxed);

        // ---- amp section (filters + drive + model), bypassable -------------
        const bool ampFullyOff =
            ampMix.getCurrentValue() <= 0.0f && ampMix.getTargetValue() <= 0.0f;
        if (ampFullyOff)
        {
            ampMix.skip(n);
            driveGain.skip(n);
        }
        else
        {
        // Dry snapshot for the bypass crossfade.
        std::memcpy(ampDry0.data(), lane0.data(), bytes);
        if (lanes == 2)
            std::memcpy(ampDry1.data(), lane1.data(), bytes);

        // ---- pre-amp filters: tight (HP) then tone (LP) --------------------
        {
            if (lanes == 1)
                std::memset(lane1.data(), 0, bytes); // constant 2ch shape
            float* ch[2] = {lane0.data(), lane1.data()};
            juce::dsp::AudioBlock<float> block{ch, 2, static_cast<size_t>(n)};
            juce::dsp::ProcessContextReplacing<float> ctx{block};

            tightFilter.setCutoffFrequency(tightHz.getNextValue());
            tightFilter.process(ctx);

            // Tone fully open means fully out of the path (even a 20 kHz
            // one-pole shades the top octave slightly).
            const float toneCut = toneHz.getNextValue();
            toneFilter.setCutoffFrequency(juce::jmin(toneCut, 19000.0f));
            if (toneCut < 19000.0f)
                toneFilter.process(ctx);
        }

        // ---- drive into the model ------------------------------------------
        applyGainRamp(driveGain, n);
        for (int s = 0; s < n; ++s)
            lane0[static_cast<size_t>(s)] *= gainRamp[static_cast<size_t>(s)];
        if (lanes == 2)
            for (int s = 0; s < n; ++s)
                lane1[static_cast<size_t>(s)] *= gainRamp[static_cast<size_t>(s)];

        // ---- amp + DC blocker ----------------------------------------------
        float* lanePtrs[2] = {lane0.data(), lane1.data()};
        engine.process(lanePtrs, lanes, n);

        // Bypass crossfade back toward the post-trim dry signal.
        applyGainRamp(ampMix, n);
        for (int s = 0; s < n; ++s)
        {
            const float mix = gainRamp[static_cast<size_t>(s)];
            lane0[static_cast<size_t>(s)] = mix * lane0[static_cast<size_t>(s)]
                                            + (1.0f - mix) * ampDry0[static_cast<size_t>(s)];
        }
        if (lanes == 2)
            for (int s = 0; s < n; ++s)
            {
                const float mix = gainRamp[static_cast<size_t>(s)];
                lane1[static_cast<size_t>(s)] =
                    mix * lane1[static_cast<size_t>(s)]
                    + (1.0f - mix) * ampDry1[static_cast<size_t>(s)];
            }
        } // ampFullyOff

        // ---- IR stage ------------------------------------------------------
        int wetLanes = lanes;
        if (haveIr && topo != IrTopology::none)
        {
            // Dry snapshot for the bypass crossfade (mono dry feeds both
            // ears when the wet side is stereo).
            std::memcpy(dry0.data(), lane0.data(), bytes);
            std::memcpy(dry1.data(), lanes == 2 ? lane1.data() : lane0.data(), bytes);

            if (topo == IrTopology::monoToStereo)
            {
                std::memcpy(lane1.data(), lane0.data(), bytes);
                wetLanes = 2;
            }
            else if (topo == IrTopology::quad)
            {
                // convPrimary: [LL,LR] fed L on both channels;
                // convQuadB:   [RL,RR] fed R on both channels.
                std::memcpy(quadB0.data(), lane1.data(), bytes);
                std::memcpy(quadB1.data(), lane1.data(), bytes);
                std::memcpy(lane1.data(), lane0.data(), bytes);
                wetLanes = 2;
            }
            else if (lanes == 1)
            {
                std::memset(lane1.data(), 0, bytes); // defined data on ch2
            }

            {
                float* ch[2] = {lane0.data(), lane1.data()};
                juce::dsp::AudioBlock<float> block{ch, 2, static_cast<size_t>(n)};
                juce::dsp::ProcessContextReplacing<float> ctx{block};
                convPrimary.process(ctx);
            }
            if (topo == IrTopology::quad)
            {
                float* ch[2] = {quadB0.data(), quadB1.data()};
                juce::dsp::AudioBlock<float> block{ch, 2, static_cast<size_t>(n)};
                juce::dsp::ProcessContextReplacing<float> ctx{block};
                convQuadB.process(ctx);
                for (int s = 0; s < n; ++s)
                {
                    lane0[static_cast<size_t>(s)] += quadB0[static_cast<size_t>(s)];
                    lane1[static_cast<size_t>(s)] += quadB1[static_cast<size_t>(s)];
                }
            }

            // Wet/dry crossfade (the IR toggle).
            applyGainRamp(irMix, n);
            for (int s = 0; s < n; ++s)
            {
                const float mix = gainRamp[static_cast<size_t>(s)];
                lane0[static_cast<size_t>(s)] = mix * lane0[static_cast<size_t>(s)]
                                                + (1.0f - mix) * dry0[static_cast<size_t>(s)];
            }
            if (wetLanes == 2)
                for (int s = 0; s < n; ++s)
                {
                    const float mix = gainRamp[static_cast<size_t>(s)];
                    lane1[static_cast<size_t>(s)] = mix * lane1[static_cast<size_t>(s)]
                                                    + (1.0f - mix) * dry1[static_cast<size_t>(s)];
                }
        }
        else
        {
            irMix.skip(n); // keep the smoother in real time
        }

        // ---- reverb send (parallel, post-cab) ------------------------------
        if (verbLoaded.load(std::memory_order_relaxed))
        {
            applyGainRamp(verbSendGain, n);
            for (int s = 0; s < n; ++s)
                verb0[static_cast<size_t>(s)] =
                    lane0[static_cast<size_t>(s)] * gainRamp[static_cast<size_t>(s)];
            const float* sendSrc = wetLanes == 2 ? lane1.data() : lane0.data();
            for (int s = 0; s < n; ++s)
                verb1[static_cast<size_t>(s)] =
                    sendSrc[s] * gainRamp[static_cast<size_t>(s)];

            {
                float* ch[2] = {verb0.data(), verb1.data()};
                juce::dsp::AudioBlock<float> block{ch, 2, static_cast<size_t>(n)};
                juce::dsp::ProcessContextReplacing<float> ctx{block};
                convVerb.process(ctx);
            }

            // A stereo verb IR widens the chain: duplicate the dry into
            // lane 1 before summing the stereo return.
            if (verbNumChannels.load(std::memory_order_relaxed) >= 2 && wetLanes == 1)
            {
                std::memcpy(lane1.data(), lane0.data(), bytes);
                wetLanes = 2;
            }
            for (int s = 0; s < n; ++s)
                lane0[static_cast<size_t>(s)] += verb0[static_cast<size_t>(s)];
            if (wetLanes == 2)
            {
                const float* ret1 = verbNumChannels.load(std::memory_order_relaxed) >= 2
                                        ? verb1.data()
                                        : verb0.data();
                for (int s = 0; s < n; ++s)
                    lane1[static_cast<size_t>(s)] += ret1[s];
            }
        }
        else
            verbSendGain.skip(n);

        // ---- output gain + write out ---------------------------------------
        applyGainRamp(outputGain, n);
        float outPeak = 0.0f;
        if (wetLanes == 1)
        {
            for (int s = 0; s < n; ++s)
            {
                lane0[static_cast<size_t>(s)] *= gainRamp[static_cast<size_t>(s)];
                outPeak = juce::jmax(outPeak, std::abs(lane0[static_cast<size_t>(s)]));
            }
            for (int c = 0; c < numOut; ++c)
                std::memcpy(buffer.getWritePointer(c) + offset, lane0.data(), bytes);
        }
        else
        {
            for (int s = 0; s < n; ++s)
            {
                const float g = gainRamp[static_cast<size_t>(s)];
                lane0[static_cast<size_t>(s)] *= g;
                lane1[static_cast<size_t>(s)] *= g;
                outPeak = juce::jmax(outPeak, juce::jmax(std::abs(lane0[static_cast<size_t>(s)]),
                                                         std::abs(lane1[static_cast<size_t>(s)])));
            }
            if (numOut >= 2)
            {
                std::memcpy(buffer.getWritePointer(0) + offset, lane0.data(), bytes);
                std::memcpy(buffer.getWritePointer(1) + offset, lane1.data(), bytes);
            }
            else if (numOut == 1)
            {
                for (int s = 0; s < n; ++s)
                    buffer.getWritePointer(0)[offset + s] =
                        0.5f * (lane0[static_cast<size_t>(s)] + lane1[static_cast<size_t>(s)]);
            }
        }
        outputPeak.store(juce::jmax(outputPeak.load(std::memory_order_relaxed), outPeak),
                         std::memory_order_relaxed);
    }
}

void Processor::resolveTopology()
{
    // Processing width.
    const int mode = static_cast<int>(channelsParam->load());
    int lanes = 1;
    if (mode == kChannelsStereo)
        lanes = 2;
    else if (mode == kChannelsAuto)
    {
        // Auto: bus width in a DAW. In the standalone a stereo device bus
        // usually carries one live channel (guitar), so Auto means mono
        // there — stereo processing is an explicit choice.
        lanes = (!juce::JUCEApplicationBase::isStandaloneApp() && busInputChannels >= 2) ? 2 : 1;
    }

    // IR topology from the file's channel count at that width.
    const int irCh = irNumChannels.load(std::memory_order_relaxed);
    IrTopology topo = IrTopology::none;
    if (irLoaded.load(std::memory_order_relaxed) && irCh > 0)
    {
        if (irCh == 1)
            topo = IrTopology::simple;
        else if (irCh == 2)
        {
            if (lanes == 1)
                topo = IrTopology::monoToStereo;
            else
                topo = static_cast<int>(stereoIrModeParam->load()) == kStereoIrMonoToStereo
                           ? IrTopology::monoToStereo // collapse handled below
                           : IrTopology::simple;      // dual mono
        }
        else // 4 (or anything >2): true stereo; mono processing uses LL/LR
            topo = lanes == 2 ? IrTopology::quad : IrTopology::monoToStereo;

        // "Mono -> stereo" of a stereo-processed signal means collapsing
        // first; simplest correct form is to process mono.
        if (topo == IrTopology::monoToStereo && lanes == 2)
            lanes = 1;
    }

    procLanes.store(lanes, std::memory_order_relaxed);
    irTopology.store(static_cast<int>(topo), std::memory_order_relaxed);
    engine.models().setLanes(lanes);
}

juce::String Processor::topologyDescription() const
{
    const int lanes = procLanes.load(std::memory_order_relaxed);
    const auto topo = static_cast<IrTopology>(irTopology.load(std::memory_order_relaxed));

    juce::String s;
    s << (busInputChannels >= 2 ? "stereo in" : "mono in");
    s << (lanes == 2 ? " \xe2\x86\x92 2\xc3\x97 amp" : " \xe2\x86\x92 amp");
    switch (topo)
    {
        case IrTopology::none: break;
        case IrTopology::simple:
            s << (lanes == 2 ? " \xe2\x86\x92 IR (dual)" : " \xe2\x86\x92 IR");
            break;
        case IrTopology::monoToStereo: s << " \xe2\x86\x92 IR (mono\xe2\x86\x92stereo)"; break;
        case IrTopology::quad: s << " \xe2\x86\x92 IR (true stereo)"; break;
    }
    const bool stereoOut =
        (lanes == 2 || topo == IrTopology::monoToStereo || topo == IrTopology::quad)
        && busOutputChannels >= 2;
    s << (stereoOut ? " \xe2\x86\x92 stereo out" : " \xe2\x86\x92 mono out");
    return s;
}

void Processor::timerCallback()
{
    const int latency = engine.latencySamples();
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    // Normalization is always on: well-tagged models land at the staged
    // bypass level; untagged ones pass through unadjusted (flagged in UI).
    const auto info = engine.models().info();
    const float offset =
        info.hasLoudness ? kNormTargetDb - static_cast<float>(info.loudness) : 0.0f;
    normalizationOffsetDb.store(offset, std::memory_order_relaxed);

    const float slim = slimParam->load();
    if (std::abs(slim - lastForwardedSlim) > 1.0e-6f)
    {
        lastForwardedSlim = slim;
        engine.models().setSlim(slim);
    }

    resolveTopology();
}

void Processor::loadIr(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader{irFormats.createReaderFor(file)};
    if (reader == nullptr)
        return;

    library.seedIrRootFrom(file);

    const int numCh = static_cast<int>(reader->numChannels);
    const auto numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> all(numCh, numSamples);
    reader->read(&all, 0, numSamples, 0, true, true);

    // Global energy normalization across ALL channels, so the four quad
    // channels keep their relative balance (per-convolution normalization
    // would skew LL/LR against RL/RR).
    double sumSq = 0.0;
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < numSamples; ++i)
            sumSq += static_cast<double>(all.getSample(c, i)) * all.getSample(c, i);
    if (sumSq <= 0.0)
        return;
    all.applyGain(static_cast<float>(1.0 / std::sqrt(sumSq)));

    auto slice = [&](int firstChannel, int channels) {
        juce::AudioBuffer<float> b(channels, numSamples);
        for (int c = 0; c < channels; ++c)
            b.copyFrom(c, 0, all, firstChannel + c, 0, numSamples);
        return b;
    };

    const double sr = reader->sampleRate;
    using Stereo = juce::dsp::Convolution::Stereo;
    using Trim = juce::dsp::Convolution::Trim;
    using Normalise = juce::dsp::Convolution::Normalise;

    if (numCh >= 4)
    {
        convPrimary.loadImpulseResponse(slice(0, 2), sr, Stereo::yes, Trim::no, Normalise::no);
        convQuadB.loadImpulseResponse(slice(2, 2), sr, Stereo::yes, Trim::no, Normalise::no);
        irNumChannels.store(4, std::memory_order_relaxed);
    }
    else if (numCh == 2)
    {
        convPrimary.loadImpulseResponse(slice(0, 2), sr, Stereo::yes, Trim::no, Normalise::no);
        irNumChannels.store(2, std::memory_order_relaxed);
    }
    else
    {
        convPrimary.loadImpulseResponse(slice(0, 1), sr, Stereo::no, Trim::no, Normalise::no);
        irNumChannels.store(1, std::memory_order_relaxed);
    }

    irPath = file.getFullPathName();
    irLoaded.store(true, std::memory_order_relaxed);
    resolveTopology();
}

void Processor::clearIr()
{
    irLoaded.store(false, std::memory_order_relaxed);
    irNumChannels.store(0, std::memory_order_relaxed);
    irPath.clear();
    resolveTopology();
}

void Processor::capturePresetSnapshot(const juce::String& pendingModelPath)
{
    presetSnapshot.valid = true;
    presetSnapshot.modelPath = pendingModelPath;
    presetSnapshot.irPath = irPath;
    presetSnapshot.drive = driveDb->load();
    presetSnapshot.quality = slimParam->load();
    presetSnapshot.irEnabled = irEnabledParam->load() >= 0.5f;
    presetSnapshot.ampEnabled = ampEnabledParam->load() >= 0.5f;
    presetSnapshot.stereoMode = static_cast<int>(stereoIrModeParam->load());
    presetSnapshot.tight = tightParam->load();
    presetSnapshot.verbSend = verbSendParam->load();
    presetSnapshot.verbPath = verbPath;
    presetSnapshot.tone = toneParam->load();
}

bool Processor::isPresetDirty() const
{
    if (!presetSnapshot.valid || currentPresetName.isEmpty())
        return false;
    const auto info = engine.models().info();
    if (juce::String{info.path} != presetSnapshot.modelPath)
        return true;
    if (irPath != presetSnapshot.irPath)
        return true;
    if (std::abs(driveDb->load() - presetSnapshot.drive) > 0.05f)
        return true;
    if (std::abs(slimParam->load() - presetSnapshot.quality) > 1.0e-3f)
        return true;
    if ((irEnabledParam->load() >= 0.5f) != presetSnapshot.irEnabled)
        return true;
    if ((ampEnabledParam->load() >= 0.5f) != presetSnapshot.ampEnabled)
        return true;
    if (static_cast<int>(stereoIrModeParam->load()) != presetSnapshot.stereoMode)
        return true;
    if (std::abs(tightParam->load() - presetSnapshot.tight) > 0.5f)
        return true;
    if (std::abs(verbSendParam->load() - presetSnapshot.verbSend) > 0.5f)
        return true;
    if (verbPath != presetSnapshot.verbPath)
        return true;
    if (std::abs(toneParam->load() - presetSnapshot.tone) > 10.0f)
        return true;
    return false;
}

void Processor::setParamFromPreset(const juce::ParameterID& id, const float naturalValue)
{
    if (auto* param = state.getParameter(id.getParamID()))
        param->setValueNotifyingHost(
            param->convertTo0to1(naturalValue));
}

bool Processor::savePreset(const juce::String& name)
{
    const auto info = engine.models().info();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", 1);
    obj->setProperty("model", info.loaded
                                  ? state::Library::toVar(library.storeModelPath(
                                        juce::File{juce::String{info.path}}))
                                  : juce::var{});
    obj->setProperty("verb", verbPath.isNotEmpty()
                                 ? state::Library::toVar(library.storeIrPath(juce::File{verbPath}))
                                 : juce::var{});
    obj->setProperty("ir", irPath.isNotEmpty()
                               ? state::Library::toVar(library.storeIrPath(juce::File{irPath}))
                               : juce::var{});

    auto* params = new juce::DynamicObject();
    params->setProperty("drive", driveDb->load());
    params->setProperty("quality", slimParam->load());
    params->setProperty("ir_enabled", irEnabledParam->load() >= 0.5f);
    params->setProperty("stereo_ir_mode", static_cast<int>(stereoIrModeParam->load()));
    params->setProperty("amp_enabled", ampEnabledParam->load() >= 0.5f);
    params->setProperty("verb_send", verbSendParam->load());
    params->setProperty("tight", tightParam->load());
    params->setProperty("tone", toneParam->load());
    // Reserved: per-model output trim for wrong-metadata models. No UI yet.
    params->setProperty("model_trim", 0.0);
    obj->setProperty("params", juce::var{params});

    const auto file = library.presetFile(name);
    file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(juce::JSON::toString(juce::var{obj}, false)))
        return false;
    currentPresetName = name;
    capturePresetSnapshot(juce::String{info.path});
    return true;
}

bool Processor::loadPreset(const juce::String& name)
{
    const auto file = library.presetFile(name);
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return false;

    juce::String pendingModelPath;
    if (obj->hasProperty("model") && obj->getProperty("model").getDynamicObject() != nullptr)
    {
        const auto resolved =
            library.resolveModelPath(state::Library::fromVar(obj->getProperty("model")));
        if (resolved != juce::File{})
        {
            pendingModelPath = resolved.getFullPathName();
            engine.models().requestLoad(pendingModelPath.toStdString());
        }
        // Unresolvable model: keep whatever is playing; the engine's error
        // surface stays quiet, but the model name won't change — visible.
    }
    else
        engine.models().requestClear();

    if (obj->hasProperty("verb") && obj->getProperty("verb").getDynamicObject() != nullptr)
    {
        const auto resolved =
            library.resolveIrPath(state::Library::fromVar(obj->getProperty("verb")));
        if (resolved != juce::File{})
            loadVerbIr(resolved);
    }
    else
        clearVerbIr();

    if (obj->hasProperty("ir") && obj->getProperty("ir").getDynamicObject() != nullptr)
    {
        const auto resolved =
            library.resolveIrPath(state::Library::fromVar(obj->getProperty("ir")));
        if (resolved != juce::File{})
            loadIr(resolved);
    }
    else
        clearIr();

    if (auto* params = obj->getProperty("params").getDynamicObject())
    {
        setParamFromPreset(state::param_ids::drive, params->getProperty("drive"));
        setParamFromPreset(state::param_ids::slim, params->getProperty("quality"));
        setParamFromPreset(state::param_ids::irEnabled,
                           static_cast<bool>(params->getProperty("ir_enabled")) ? 1.0f : 0.0f);
        setParamFromPreset(state::param_ids::stereoIrMode,
                           static_cast<int>(params->getProperty("stereo_ir_mode")));
        if (params->hasProperty("amp_enabled"))
            setParamFromPreset(state::param_ids::ampEnabled,
                               static_cast<bool>(params->getProperty("amp_enabled")) ? 1.0f
                                                                                     : 0.0f);
        if (params->hasProperty("verb_send"))
            setParamFromPreset(state::param_ids::verbSend, params->getProperty("verb_send"));
        if (params->hasProperty("tight"))
            setParamFromPreset(state::param_ids::tight, params->getProperty("tight"));
        if (params->hasProperty("tone"))
            setParamFromPreset(state::param_ids::tone, params->getProperty("tone"));
    }

    currentPresetName = name;
    capturePresetSnapshot(pendingModelPath);
    return true;
}

void Processor::deletePreset(const juce::String& name)
{
    library.presetFile(name).deleteFile();
    if (currentPresetName == name)
        currentPresetName.clear();
}

void Processor::loadVerbIr(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader{irFormats.createReaderFor(file)};
    if (reader == nullptr)
        return;

    library.seedIrRootFrom(file);

    const int numCh = static_cast<int>(reader->numChannels);
    const auto numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> all(numCh, numSamples);
    reader->read(&all, 0, numSamples, 0, true, true);

    double sumSq = 0.0;
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < numSamples; ++i)
            sumSq += static_cast<double>(all.getSample(c, i)) * all.getSample(c, i);
    if (sumSq <= 0.0)
        return;
    all.applyGain(static_cast<float>(1.0 / std::sqrt(sumSq)));

    juce::AudioBuffer<float> b(juce::jmin(numCh, 2), numSamples);
    for (int c = 0; c < b.getNumChannels(); ++c)
        b.copyFrom(c, 0, all, c, 0, numSamples);

    using Stereo = juce::dsp::Convolution::Stereo;
    using Trim = juce::dsp::Convolution::Trim;
    using Normalise = juce::dsp::Convolution::Normalise;
    convVerb.loadImpulseResponse(std::move(b), reader->sampleRate,
                                 numCh >= 2 ? Stereo::yes : Stereo::no, Trim::no,
                                 Normalise::no);
    verbNumChannels.store(juce::jmin(numCh, 2), std::memory_order_relaxed);
    verbPath = file.getFullPathName();
    verbLoaded.store(true, std::memory_order_relaxed);
}

void Processor::clearVerbIr()
{
    verbLoaded.store(false, std::memory_order_relaxed);
    verbNumChannels.store(0, std::memory_order_relaxed);
    verbPath.clear();
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}

void Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty("stateVersion", state::kStateVersion, nullptr);
    const auto info = engine.models().info();
    if (info.loaded)
    {
        const auto sp = library.storeModelPath(juce::File{juce::String{info.path}});
        tree.setProperty("modelPath", juce::String{sp.absolute}, nullptr);
        tree.setProperty("modelRel", juce::String{sp.relative}, nullptr);
        tree.setProperty("modelFile", juce::String{sp.filename}, nullptr);
    }
    if (irPath.isNotEmpty())
    {
        const auto sp = library.storeIrPath(juce::File{irPath});
        tree.setProperty("irPath", juce::String{sp.absolute}, nullptr);
        tree.setProperty("irRel", juce::String{sp.relative}, nullptr);
        tree.setProperty("irFile", juce::String{sp.filename}, nullptr);
    }
    if (verbPath.isNotEmpty())
    {
        const auto sp = library.storeIrPath(juce::File{verbPath});
        tree.setProperty("verbPath", juce::String{sp.absolute}, nullptr);
        tree.setProperty("verbRel", juce::String{sp.relative}, nullptr);
        tree.setProperty("verbFile", juce::String{sp.filename}, nullptr);
    }
    tree.setProperty("presetName", currentPresetName, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void Processor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml(*xml);

            state::StoredPath model;
            model.absolute = tree.getProperty("modelPath", juce::String{}).toString().toStdString();
            model.relative = tree.getProperty("modelRel", juce::String{}).toString().toStdString();
            model.filename = tree.getProperty("modelFile", juce::String{}).toString().toStdString();
            if (!model.empty())
            {
                const auto resolved = library.resolveModelPath(model);
                if (resolved != juce::File{})
                    engine.models().requestLoad(resolved.getFullPathName().toStdString());
            }

            state::StoredPath ir;
            ir.absolute = tree.getProperty("irPath", juce::String{}).toString().toStdString();
            ir.relative = tree.getProperty("irRel", juce::String{}).toString().toStdString();
            ir.filename = tree.getProperty("irFile", juce::String{}).toString().toStdString();
            if (!ir.empty())
            {
                const auto resolved = library.resolveIrPath(ir);
                if (resolved != juce::File{})
                    loadIr(resolved);
            }

            state::StoredPath verb;
            verb.absolute = tree.getProperty("verbPath", juce::String{}).toString().toStdString();
            verb.relative = tree.getProperty("verbRel", juce::String{}).toString().toStdString();
            verb.filename = tree.getProperty("verbFile", juce::String{}).toString().toStdString();
            if (!verb.empty())
            {
                const auto resolved = library.resolveIrPath(verb);
                if (resolved != juce::File{})
                    loadVerbIr(resolved);
            }

            currentPresetName = tree.getProperty("presetName", juce::String{}).toString();
            state.replaceState(tree);
        }
    }
}

} // namespace namrig

// JUCE entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new namrig::Processor();
}
