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
    int numLanes = 0;
    double nativeSampleRate = 0.0;
    bool hasLoudness = false;
    double loudness = 0.0;
    bool slimmable = false;
    // Non-empty when the most recent load failed; `loaded` then reflects
    // whatever model (if any) is still active from before.
    std::string error;
};

// One or two instances of the same model (dual-mono stereo), built together
// on the worker and swapped in/retired as a single unit.
struct ModelPair
{
    std::array<std::unique_ptr<ResamplingNam>, 2> lane;
    int numLanes = 0;
};

// Drive points (dB) of the measured gain-rise curve.
inline constexpr std::array<float, 5> kDrivePointsDb{-20.0f, -10.0f, 0.0f, 10.0f, 20.0f};

// Owns the lifecycle of the active NAM model pair and enforces the
// threading rules from CLAUDE.md:
//
//  * Loads happen on a private worker thread (parse, allocate, prewarm).
//  * The finished pair is published through an atomic slot; the audio
//    thread swaps it in at the top of a block (render()).
//  * The displaced pair goes into a lock-free retire ring; the worker
//    drains the ring and frees off-thread. The audio thread never frees.
//  * Slim is applied on the worker thread; the core's SlimmableWavenet
//    stages its rebuild internally, so this is safe against process().
//  * Lane-count changes rebuild from the stored path on the worker and
//    arrive as a normal swap.
//
// prepare() may only run while audio is stopped (JUCE's prepareToPlay
// contract) — it is the one place the message thread touches the current
// pair directly.
class ModelSlot
{
public:
    ModelSlot();
    ~ModelSlot();

    // --- message thread ---
    void prepare(double sampleRate, int maxBlockSize); // audio stopped
    void requestLoad(const std::filesystem::path& path);
    void requestClear();
    void setSlim(double value);  // 0..1; applied to current and future pairs
    void setLanes(int numLanes); // 1 or 2; rebuilds the loaded model if needed
    ModelInfo info() const;

    int latencySamples() const { return latency.load(std::memory_order_relaxed); }

    // Measured output-level rise (dB, relative to drive 0) for a given drive,
    // interpolated from the load-time sweep of the current model. Lock-free;
    // any thread. 'driveDb' for a linear model returns ~driveDb; compressing
    // models return less — subtract THIS, not the nominal drive, to keep
    // loudness flat.
    float measuredRiseDb(float driveDb) const;

    // --- audio thread ---
    // Swaps in any pending pair and returns the active one (nullptr = bypass).
    ModelPair* render();

private:
    void workerLoop();
    void drainRetired();                 // worker thread
    void destroyOwned(ModelPair* p);     // worker/prepare, under ownedMutex
    void loadJob(const std::filesystem::path& path);

    // Audio-thread-owned active pair. The message thread may touch it only
    // inside prepare() (audio stopped) and the destructor (audio torn down).
    ModelPair* current = nullptr;

    // Worker -> audio handoff. Non-null = a finished pair awaiting swap-in.
    std::atomic<ModelPair*> pending{nullptr};
    std::atomic<bool> clearRequested{false};
    std::atomic<int> latency{0};

    // Audio -> worker retire ring (SPSC). Full ring = swap deferred a block.
    static constexpr size_t kRetireSlots = 64;
    std::array<ModelPair*, kRetireSlots> retireRing{};
    std::atomic<size_t> retireHead{0}, retireTail{0};
    bool pushRetired(ModelPair* p); // audio thread

    // Every pair this slot creates lives here until freed; guarded by
    // ownedMutex (worker + rare message-thread use, never audio).
    std::vector<std::unique_ptr<ModelPair>> owned;
    mutable std::mutex ownedMutex;
    ModelPair* lastPublished = nullptr; // worker bookkeeping for slim

    // Engine format, set by prepare(); worker re-checks after each build.
    std::atomic<double> engineSampleRate{48000.0};
    std::atomic<int> engineMaxBlock{512};
    std::atomic<bool> prepared{false};

    std::atomic<double> slim{1.0};
    std::atomic<int> desiredLanes{1};

    // Gain-rise curve of the most recently published model (written by the
    // worker just before publish; identity curve when nothing is loaded).
    std::array<std::atomic<float>, kDrivePointsDb.size()> riseDb{};
    void measureRiseCurve(ResamplingNam& lane); // worker thread

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
