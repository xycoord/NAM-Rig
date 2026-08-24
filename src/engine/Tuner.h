#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace namrig::engine
{

// Chromatic pitch detector (McLeod / NSDF autocorrelation).
//
// Threading: the audio thread push()es post-trim mono samples into a
// lock-free SPSC ring; a low-priority worker drains it, decimates to
// ~24 kHz, and analyzes ~85 ms windows. Results are published as atomics.
// Analysis runs only while active (editor open) — otherwise push() is a
// near-no-op and the worker sleeps.
class Tuner
{
public:
    Tuner();
    ~Tuner();

    // --- message thread ---
    void prepare(double sampleRate); // audio stopped
    void setActive(bool shouldAnalyze);

    // Detected fundamental in Hz (0 = no confident pitch) and clarity 0..1.
    float frequencyHz() const { return frequency.load(std::memory_order_relaxed); }
    float clarity() const { return clarityValue.load(std::memory_order_relaxed); }

    // --- audio thread ---
    void push(const float* samples, int numFrames);

private:
    void workerLoop();
    void analyze();

    // SPSC ring, audio -> worker.
    static constexpr size_t kRingSize = 1 << 14;
    std::array<float, kRingSize> ring{};
    std::atomic<size_t> ringHead{0}, ringTail{0};

    std::atomic<bool> active{false};
    std::atomic<bool> quit{false};
    std::atomic<float> frequency{0.0f};
    std::atomic<float> clarityValue{0.0f};

    std::atomic<double> sourceRate{48000.0};

    // Worker-only analysis state.
    std::vector<float> window;   // sliding analysis buffer (decimated rate)
    std::vector<float> nsdf;     // scratch
    double decimatedRate = 24000.0;
    int decimation = 2;
    float decimAccum = 0.0f;
    int decimCount = 0;

    std::mutex wakeMutex;
    std::condition_variable wakeCv;
    std::thread worker;

    static constexpr double kMinHz = 40.0, kMaxHz = 1000.0;
    static constexpr float kClarityThreshold = 0.80f;
};

} // namespace namrig::engine
