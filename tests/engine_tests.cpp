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
    namrig::engine::ResamplingNam* model = nullptr;
    for (int i = 0; i < 100 && model == nullptr; ++i)
    {
        model = slot.render();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(model != nullptr);

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

    namrig::engine::ResamplingNam* model = nullptr;
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
        model->process(in, out, 256);
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
        engine.process(signal.data() + b * blockSize, blockSize);

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
    engine.process(signal.data(), 4096);
    CHECK(allFinite(signal));
}

TEST_CASE("Engine renders a loaded model with finite output")
{
    namrig::engine::Engine engine;
    engine.prepare(48000.0, 256);

    engine.models().requestLoad(kModelsDir / "wavenet.nam");
    REQUIRE(waitForLoad(engine.models(), std::chrono::seconds(30)));

    auto signal = makeSine(256, 48000.0);
    std::vector<float> lastBlock;
    for (int i = 0; i < 50; ++i)
    {
        lastBlock = makeSine(256, 48000.0);
        engine.process(lastBlock.data(), 256);
    }

    CHECK(allFinite(lastBlock));
    CHECK(peak(lastBlock) > 0.0f); // an amp model should produce *something*
}
