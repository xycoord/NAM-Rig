// Engine tests: offline renders through the real NAM core using the example
// models the core ships. No JUCE anywhere (the engine is JUCE-free by rule).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

#include "NAM/get_dsp.h"
#include "engine/Engine.h"
#include "engine/ModelSlot.h"
#include "engine/ResamplingNam.h"

namespace
{
const std::filesystem::path kModelsDir{NAMRIG_TEST_MODELS_DIR};

std::vector<float> makeSine(const int numFrames, const double sampleRate, const double hz = 220.0,
                            const float amplitude = 0.1f)
{
    std::vector<float> v(static_cast<size_t>(numFrames));
    for (int i = 0; i < numFrames; ++i)
        v[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * i / sampleRate));
    return v;
}

bool allFinite(const std::vector<float>& v)
{
    for (const float x : v)
        if (!std::isfinite(x))
            return false;
    return true;
}

float peak(const std::vector<float>& v)
{
    float p = 0.0f;
    for (const float x : v)
        p = std::max(p, std::abs(x));
    return p;
}

// Polls until the slot reports a loaded model (loads happen on the worker).
bool waitForLoad(namrig::engine::ModelSlot& slot, const std::chrono::seconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto info = slot.info();
        if (info.loaded || !info.error.empty())
            return info.loaded;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

TEST_CASE("example models are present (submodule initialized)")
{
    REQUIRE(std::filesystem::exists(kModelsDir / "wavenet.nam"));
}

TEST_CASE("ResamplingNam at native rate has no latency and produces finite audio")
{
    auto model = nam::get_dsp(kModelsDir / "wavenet.nam");
    REQUIRE(model != nullptr);

    const double rate = namrig::engine::modelSampleRate(*model);
    const int block = 256;
    namrig::engine::ResamplingNam wrapped(std::move(model), rate, block);

    CHECK(wrapped.latency() == 0);

    auto input = makeSine(block, rate);
    std::vector<float> output(static_cast<size_t>(block), 0.0f);
    float* in[1] = {input.data()};
    float* out[1] = {output.data()};

    for (int i = 0; i < 20; ++i) // several blocks: past any settling
        wrapped.process(in, out, block);

    CHECK(allFinite(output));
}

TEST_CASE("ResamplingNam reports latency when resampling")
{
    auto model = nam::get_dsp(kModelsDir / "wavenet.nam");
    const double nativeRate = namrig::engine::modelSampleRate(*model);
    const double externalRate = nativeRate == 44100.0 ? 48000.0 : 44100.0;

    namrig::engine::ResamplingNam wrapped(std::move(model), externalRate, 256);
    CHECK(wrapped.latency() > 0);
}

TEST_CASE("ModelSlot loads, swaps, clears")
{
    namrig::engine::ModelSlot slot;
    slot.prepare(48000.0, 256);

    slot.requestLoad(kModelsDir / "wavenet.nam");
    REQUIRE(waitForLoad(slot, std::chrono::seconds(30)));

    // The "audio thread" (this one) swaps the model in on render().
    namrig::engine::ModelPair* model = nullptr;
    for (int i = 0; i < 100 && model == nullptr; ++i)
    {
        model = slot.render();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(model != nullptr);
    CHECK(model->numLanes == 1);

    auto info = slot.info();
    CHECK(info.loaded);
    CHECK(info.error.empty());
    CHECK(info.nativeSampleRate > 0.0);

    slot.requestClear();
    for (int i = 0; i < 100 && slot.render() != nullptr; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(slot.render() == nullptr);
    CHECK_FALSE(slot.info().loaded);
}

TEST_CASE("ModelSlot reports errors and keeps playing")
{
    namrig::engine::ModelSlot slot;
    slot.prepare(48000.0, 256);

    slot.requestLoad(kModelsDir / "does_not_exist.nam");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (slot.info().error.empty() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    CHECK_FALSE(slot.info().error.empty());
    CHECK(slot.render() == nullptr); // nothing was ever loaded
}

TEST_CASE("ModelSlot applies slim to slimmable models")
{
    namrig::engine::ModelSlot slot;
    slot.prepare(48000.0, 256);

    slot.requestLoad(kModelsDir / "slimmable_wavenet.nam");
    REQUIRE(waitForLoad(slot, std::chrono::seconds(30)));
    CHECK(slot.info().slimmable);

    namrig::engine::ModelPair* model = nullptr;
    for (int i = 0; i < 100 && model == nullptr; ++i)
    {
        model = slot.render();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(model != nullptr);

    // Change slim while "audio" keeps processing: the core stages the
    // rebuild; nothing should glitch, crash, or go non-finite.
    slot.setSlim(0.0);
    auto input = makeSine(256, 48000.0);
    std::vector<float> output(256, 0.0f);
    float* in[1] = {input.data()};
    float* out[1] = {output.data()};
    for (int i = 0; i < 50; ++i)
    {
        model = slot.render();
        REQUIRE(model != nullptr);
        model->lane[0]->process(in, out, 256);
    }
    CHECK(allFinite(output));
}

TEST_CASE("Engine passthrough without a model is identity plus DC blocker")
{
    namrig::engine::Engine engine;
    engine.prepare(48000.0, 512);

    // One continuous sine processed block-by-block (the filter keeps state;
    // re-processing the same block would hand it a phase discontinuity).
    const int blocks = 20, blockSize = 512;
    auto signal = makeSine(blocks * blockSize, 48000.0);
    const auto original = signal;
    for (int b = 0; b < blocks; ++b)
    {
        float* lanes[1] = {signal.data() + b * blockSize};
        engine.process(lanes, 1, blockSize);
    }

    CHECK(allFinite(signal));
    // After settling, a 5 Hz high-pass leaves a 220 Hz sine essentially
    // untouched. Compare the final block only.
    float maxDelta = 0.0f;
    for (size_t i = static_cast<size_t>((blocks - 1) * blockSize); i < signal.size(); ++i)
        maxDelta = std::max(maxDelta, std::abs(signal[i] - original[i]));
    CHECK(maxDelta < 0.01f);
}

TEST_CASE("Engine chunks oversized blocks without reallocation")
{
    namrig::engine::Engine engine;
    engine.prepare(48000.0, 128);

    // 4096 frames through an engine prepared for 128: must chunk, not die.
    auto signal = makeSine(4096, 48000.0);
    float* lanes[1] = {signal.data()};
    engine.process(lanes, 1, 4096);
    CHECK(allFinite(signal));
}

TEST_CASE("Engine renders a loaded model with finite output")
{
    namrig::engine::Engine engine;
    engine.prepare(48000.0, 256);

    engine.models().requestLoad(kModelsDir / "wavenet.nam");
    REQUIRE(waitForLoad(engine.models(), std::chrono::seconds(30)));

    std::vector<float> lastBlock;
    for (int i = 0; i < 50; ++i)
    {
        lastBlock = makeSine(256, 48000.0);
        float* lanes[1] = {lastBlock.data()};
        engine.process(lanes, 1, 256);
    }

    CHECK(allFinite(lastBlock));
    CHECK(peak(lastBlock) > 0.0f); // an amp model should produce *something*
}


TEST_CASE("stereo lanes process independently through a model pair")
{
    namrig::engine::Engine engine;
    engine.prepare(48000.0, 256);
    engine.models().setLanes(2);

    engine.models().requestLoad(kModelsDir / "wavenet.nam");
    REQUIRE(waitForLoad(engine.models(), std::chrono::seconds(30)));
    REQUIRE(engine.models().info().numLanes == 2);

    // Distinct signals per lane: a sine left, silence right. After enough
    // blocks the lanes must differ (left carries signal) and stay finite.
    std::vector<float> left, right;
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < 50; ++i)
    {
        left = makeSine(256, 48000.0);
        right.assign(256, 0.0f);
        float* lanes[2] = {left.data(), right.data()};
        engine.process(lanes, 2, 256);
        peakL = std::max(peakL, peak(left));
        peakR = std::max(peakR, peak(right));
    }

    CHECK(allFinite(left));
    CHECK(allFinite(right));
    CHECK(peakL > 0.001f);
    // The silent lane may carry model bias/noise but must be far below the
    // driven lane.
    CHECK(peakR < peakL * 0.5f);
}

TEST_CASE("lane count change rebuilds the loaded model")
{
    namrig::engine::ModelSlot slot;
    slot.prepare(48000.0, 256);
    slot.requestLoad(kModelsDir / "wavenet.nam");
    REQUIRE(waitForLoad(slot, std::chrono::seconds(30)));
    CHECK(slot.info().numLanes == 1);

    slot.setLanes(2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (slot.info().numLanes != 2 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(slot.info().numLanes == 2);
}


TEST_CASE("measured gain-rise curve is sane for a real model")
{
    namrig::engine::ModelSlot slot;
    slot.prepare(48000.0, 256);

    // Identity before any load: rise == drive.
    CHECK(std::abs(slot.measuredRiseDb(10.0f) - 10.0f) < 1.0e-3f);

    slot.requestLoad(kModelsDir / "wavenet.nam");
    REQUIRE(waitForLoad(slot, std::chrono::seconds(60)));

    // By construction the centre point is 0.
    CHECK(std::abs(slot.measuredRiseDb(0.0f)) < 1.0e-3f);
    // Monotonic: more drive never gets quieter overall.
    const float lo = slot.measuredRiseDb(-20.0f);
    const float hi = slot.measuredRiseDb(20.0f);
    CHECK(lo < 0.0f);
    CHECK(hi > 0.0f);
    // A real amp capture can't rise faster than linear across +/-20 dB by
    // any large margin, and compression means it usually rises less.
    CHECK(hi <= 21.0f);
    CHECK(lo >= -21.0f);
}
