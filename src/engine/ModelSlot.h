#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ResamplingNam.h"

namespace namrig::engine
{

// Message-thread snapshot of what's loaded. Never touched by the audio thread.
struct ModelInfo
{
    bool loaded = false;
    std::string path;
    double nativeSampleRate = 0.0;
    bool hasLoudness = false;
    double loudness = 0.0;
    bool slimmable = false;
    // Non-empty when the most recent load failed; `loaded` then reflects
    // whatever model (if any) is still active from before.
    std::string error;
};

// Owns the lifecycle of the active NAM model and enforces the threading
// rules from CLAUDE.md:
//
//  * Loads happen on a private worker thread (parse, allocate, prewarm).
//  * The finished model is published through an atomic slot; the audio
//    thread swaps it in at the top of a block (render()).
//  * The displaced model goes into a lock-free retire ring; the worker
//    drains the ring and frees off-thread. The audio thread never frees.
//  * Slim is applied on the worker thread; the core's SlimmableWavenet
//    stages its rebuild internally, so this is safe against process().
//
// prepare() may only run while audio is stopped (JUCE's prepareToPlay
// contract) — it is the one place the message thread touches the current
// model directly.
class ModelSlot
{
public:
    ModelSlot();
    ~ModelSlot();

    // --- message thread ---
    void prepare(double sampleRate, int maxBlockSize); // audio stopped
    void requestLoad(const std::filesystem::path& path);
    void requestClear();
    void setSlim(double value); // 0..1; applied to current and future models
    ModelInfo info() const;

    // Latency of the active model's resampler, updated when the audio thread
    // swaps models (and by prepare()). Safe from any thread.
    int latencySamples() const { return latency.load(std::memory_order_relaxed); }

    // --- audio thread ---
    // Swaps in any pending model and returns the active one (nullptr = bypass).
    ResamplingNam* render();

private:
    void workerLoop();
    void drainRetired();                        // worker thread
    void destroyOwned(ResamplingNam* p);        // worker/prepare, under ownedMutex
    void loadJob(const std::filesystem::path& path);

    // Audio-thread-owned active model. The message thread may touch it only
    // inside prepare() (audio stopped) and the destructor (audio torn down).
    ResamplingNam* current = nullptr;

    // Worker -> audio handoff. Non-null = a finished model awaiting swap-in.
    std::atomic<ResamplingNam*> pending{nullptr};
    std::atomic<bool> clearRequested{false};
    std::atomic<int> latency{0};

    // Audio -> worker retire ring (SPSC). Full ring = swap deferred a block.
    static constexpr size_t kRetireSlots = 64;
    std::array<ResamplingNam*, kRetireSlots> retireRing{};
    std::atomic<size_t> retireHead{0}, retireTail{0};
    bool pushRetired(ResamplingNam* p); // audio thread

    // Every model this slot creates lives here until freed; guarded by
    // ownedMutex (worker + rare message-thread use, never audio).
    std::vector<std::unique_ptr<ResamplingNam>> owned;
    mutable std::mutex ownedMutex;
    ResamplingNam* lastPublished = nullptr; // worker bookkeeping for slim

    // Engine format, set by prepare(); worker re-checks after each build.
    std::atomic<double> engineSampleRate{48000.0};
    std::atomic<int> engineMaxBlock{512};
    std::atomic<bool> prepared{false};

    std::atomic<double> slim{1.0};

    // Worker job queue: latest-wins for loads.
    std::mutex jobMutex;
    std::condition_variable jobCv;
    std::filesystem::path requestedPath;
    bool loadRequested = false;
    bool slimRequested = false;
    bool quitRequested = false;

    mutable std::mutex infoMutex;
    ModelInfo currentInfo;

    std::thread worker;
};

} // namespace namrig::engine
