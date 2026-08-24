#include "Tuner.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace namrig::engine
{

Tuner::Tuner()
{
    worker = std::thread([this] { workerLoop(); });
}

Tuner::~Tuner()
{
    quit.store(true);
    wakeCv.notify_all();
    worker.join();
}

void Tuner::prepare(const double sampleRate)
{
    sourceRate.store(sampleRate, std::memory_order_relaxed);
}

void Tuner::setActive(const bool shouldAnalyze)
{
    active.store(shouldAnalyze, std::memory_order_relaxed);
    if (!shouldAnalyze)
    {
        frequency.store(0.0f, std::memory_order_relaxed);
        clarityValue.store(0.0f, std::memory_order_relaxed);
    }
    wakeCv.notify_all();
}

void Tuner::push(const float* samples, const int numFrames)
{
    if (!active.load(std::memory_order_relaxed))
        return;

    size_t head = ringHead.load(std::memory_order_relaxed);
    const size_t tail = ringTail.load(std::memory_order_acquire);
    for (int i = 0; i < numFrames; ++i)
    {
        const size_t next = (head + 1) & (kRingSize - 1);
        if (next == tail)
            break; // full: drop the newest — the worker will catch up
        ring[head] = samples[i];
        head = next;
    }
    ringHead.store(head, std::memory_order_release);
}

void Tuner::workerLoop()
{
    while (!quit.load(std::memory_order_relaxed))
    {
        {
            std::unique_lock<std::mutex> lock(wakeMutex);
            wakeCv.wait_for(lock, std::chrono::milliseconds(40), [this] {
                return quit.load(std::memory_order_relaxed);
            });
        }
        if (quit.load(std::memory_order_relaxed))
            return;
        if (!active.load(std::memory_order_relaxed))
        {
            // Discard anything queued while inactive.
            ringTail.store(ringHead.load(std::memory_order_acquire),
                           std::memory_order_release);
            window.clear();
            continue;
        }

        // Refresh decimation for the current source rate.
        const double rate = sourceRate.load(std::memory_order_relaxed);
        decimation = std::max(1, static_cast<int>(std::lround(rate / 24000.0)));
        decimatedRate = rate / decimation;
        const auto windowSize =
            static_cast<size_t>(decimatedRate * 0.085); // ~85 ms

        // Drain the ring, decimating by boxcar average (crude AA is fine —
        // we only care about content well below 6 kHz).
        size_t tail = ringTail.load(std::memory_order_relaxed);
        const size_t head = ringHead.load(std::memory_order_acquire);
        while (tail != head)
        {
            decimAccum += ring[tail];
            if (++decimCount == decimation)
            {
                window.push_back(decimAccum / static_cast<float>(decimation));
                decimAccum = 0.0f;
                decimCount = 0;
            }
            tail = (tail + 1) & (kRingSize - 1);
        }
        ringTail.store(tail, std::memory_order_release);

        if (window.size() > windowSize * 2)
            window.erase(window.begin(),
                         window.begin()
                             + static_cast<long>(window.size() - windowSize));

        if (window.size() >= windowSize)
            analyze();
    }
}

void Tuner::analyze()
{
    const auto n = window.size();
    const float* x = window.data() + (window.size() - n);

    const auto minLag = static_cast<size_t>(decimatedRate / kMaxHz);
    const auto maxLag = std::min(n - 1, static_cast<size_t>(decimatedRate / kMinHz));
    if (maxLag <= minLag + 2)
        return;

    // Gate: don't chase the noise floor.
    double power = 0.0;
    for (size_t i = 0; i < n; ++i)
        power += static_cast<double>(x[i]) * x[i];
    if (power / static_cast<double>(n) < 1.0e-6) // ~ -60 dBFS RMS
    {
        frequency.store(0.0f, std::memory_order_relaxed);
        clarityValue.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // NSDF: n'(tau) = 2*sum(x_i * x_{i+tau}) / sum(x_i^2 + x_{i+tau}^2)
    nsdf.assign(maxLag + 1, 0.0f);
    for (size_t tau = minLag; tau <= maxLag; ++tau)
    {
        double acf = 0.0, m = 0.0;
        const size_t len = n - tau;
        for (size_t i = 0; i < len; ++i)
        {
            const double a = x[i], b = x[i + tau];
            acf += a * b;
            m += a * a + b * b;
        }
        nsdf[tau] = m > 0.0 ? static_cast<float>(2.0 * acf / m) : 0.0f;
    }

    // Peak picking: collect maxima between negative-going zero crossings,
    // then take the FIRST peak within k of the global max (McLeod).
    struct Peak
    {
        size_t lag;
        float value;
    };
    std::vector<Peak> peaks;
    float best = 0.0f;
    size_t tau = minLag;
    while (tau <= maxLag && nsdf[tau] > 0.0f)
        ++tau; // skip the initial lobe
    while (tau <= maxLag)
    {
        // find next positive region's max
        while (tau <= maxLag && nsdf[tau] <= 0.0f)
            ++tau;
        Peak p{0, 0.0f};
        while (tau <= maxLag && nsdf[tau] > 0.0f)
        {
            if (nsdf[tau] > p.value)
                p = {tau, nsdf[tau]};
            ++tau;
        }
        if (p.lag != 0)
        {
            peaks.push_back(p);
            best = std::max(best, p.value);
        }
    }

    if (peaks.empty() || best < kClarityThreshold)
    {
        frequency.store(0.0f, std::memory_order_relaxed);
        clarityValue.store(best, std::memory_order_relaxed);
        return;
    }

    const float threshold = 0.9f * best;
    const auto chosen =
        *std::find_if(peaks.begin(), peaks.end(),
                      [threshold](const Peak& p) { return p.value >= threshold; });

    // Parabolic interpolation around the chosen lag for sub-sample precision.
    double lag = static_cast<double>(chosen.lag);
    if (chosen.lag > minLag && chosen.lag < maxLag)
    {
        const double a = nsdf[chosen.lag - 1], b = nsdf[chosen.lag],
                     c = nsdf[chosen.lag + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1.0e-12)
            lag += 0.5 * (a - c) / denom;
    }

    frequency.store(static_cast<float>(decimatedRate / lag), std::memory_order_relaxed);
    clarityValue.store(chosen.value, std::memory_order_relaxed);
}

} // namespace namrig::engine
