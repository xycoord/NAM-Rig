#include "ModelSlot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "NAM/get_dsp.h"

#include "Architectures.h"

namespace namrig::engine
{

ModelSlot::ModelSlot()
{
    registerBuiltinArchitectures();
    // Identity curve until a model is measured.
    for (size_t i = 0; i < kDrivePointsDb.size(); ++i)
        riseDb[i].store(kDrivePointsDb[i], std::memory_order_relaxed);
    worker = std::thread([this] { workerLoop(); });
}

float ModelSlot::measuredRiseDb(const float driveDb) const
{
    const auto& pts = kDrivePointsDb;
    if (driveDb <= pts.front())
        return riseDb[0].load(std::memory_order_relaxed);
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (driveDb <= pts[i])
        {
            const float x0 = pts[i - 1], x1 = pts[i];
            const float y0 = riseDb[i - 1].load(std::memory_order_relaxed);
            const float y1 = riseDb[i].load(std::memory_order_relaxed);
            const float t = (driveDb - x0) / (x1 - x0);
            return y0 + t * (y1 - y0);
        }
    }
    return riseDb[pts.size() - 1].load(std::memory_order_relaxed);
}

void ModelSlot::measureRiseCurve(ResamplingNam& lane)
{
    // Feed a 220 Hz tone at each drive point, measure steady-state output
    // RMS, and store the rise relative to the drive-0 point. The model is
    // Reset afterwards so no measurement state reaches the audio thread.
    const double rate = engineSampleRate.load(std::memory_order_relaxed);
    const int block = std::min(engineMaxBlock.load(std::memory_order_relaxed), 256);
    const float baseAmplitude = 0.1f; // ~-20 dBFS peak, a hot guitar-ish level
    const int settleBlocks = 40, measureBlocks = 40;

    std::vector<float> in(static_cast<size_t>(block)), out(static_cast<size_t>(block));
    std::array<double, kDrivePointsDb.size()> levelDb{};

    double phase = 0.0;
    const double phaseInc = 2.0 * 3.14159265358979323846 * 220.0 / rate;

    for (size_t p = 0; p < kDrivePointsDb.size(); ++p)
    {
        const float amp = baseAmplitude * std::pow(10.0f, kDrivePointsDb[p] / 20.0f);
        double sumSq = 0.0;
        size_t count = 0;
        for (int b = 0; b < settleBlocks + measureBlocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                in[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(phase));
                phase += phaseInc;
            }
            float* ip[1] = {in.data()};
            float* op[1] = {out.data()};
            lane.process(ip, op, block);
            if (b >= settleBlocks)
            {
                for (int i = 0; i < block; ++i)
                    sumSq += static_cast<double>(out[static_cast<size_t>(i)])
                             * out[static_cast<size_t>(i)];
                count += static_cast<size_t>(block);
            }
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(count));
        levelDb[p] = 20.0 * std::log10(std::max(rms, 1.0e-9));
    }

    const double centre = levelDb[kDrivePointsDb.size() / 2];
    for (size_t p = 0; p < kDrivePointsDb.size(); ++p)
        riseDb[p].store(static_cast<float>(levelDb[p] - centre), std::memory_order_relaxed);

    // Back to a clean, prewarmed state before this instance goes live.
    lane.Reset(rate, engineMaxBlock.load(std::memory_order_relaxed));
}

ModelSlot::~ModelSlot()
{
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        quitRequested = true;
    }
    jobCv.notify_all();
    worker.join();

    // Audio is torn down before the processor (and this slot) is destroyed,
    // so touching audio-side state here is safe.
    drainRetired();
    if (auto* p = pending.exchange(nullptr))
        destroyOwned(p);
    if (current != nullptr)
        destroyOwned(current);
}

// --- audio thread ------------------------------------------------------------

bool ModelSlot::pushRetired(ModelPair* p)
{
    const size_t head = retireHead.load(std::memory_order_relaxed);
    const size_t next = (head + 1) % kRetireSlots;
    if (next == retireTail.load(std::memory_order_acquire))
        return false; // full — caller keeps things as they are this block
    retireRing[head] = p;
    retireHead.store(next, std::memory_order_release);
    return true;
}

ModelPair* ModelSlot::render()
{
    if (clearRequested.load(std::memory_order_relaxed))
    {
        if (current == nullptr || pushRetired(current))
        {
            current = nullptr;
            latency.store(0, std::memory_order_relaxed);
            clearRequested.store(false, std::memory_order_release);
        }
    }

    if (ModelPair* incoming = pending.exchange(nullptr, std::memory_order_acq_rel))
    {
        if (current == nullptr || pushRetired(current))
        {
            current = incoming;
            latency.store(current->lane[0]->latency(), std::memory_order_relaxed);
        }
        else
        {
            // Retire ring full (pathological). Defer the swap: put the new
            // pair back and try again next block.
            pending.store(incoming, std::memory_order_release);
        }
    }

    return current;
}

// --- message thread -----------------------------------------------------------

void ModelSlot::prepare(const double sampleRate, const int maxBlockSize)
{
    engineSampleRate.store(sampleRate, std::memory_order_relaxed);
    engineMaxBlock.store(maxBlockSize, std::memory_order_relaxed);
    prepared.store(true, std::memory_order_release);
    jobCv.notify_all(); // a load may be parked waiting for prepare()

    // Audio is stopped (prepareToPlay contract): the current pair and the
    // pending slot are safe to touch directly.
    if (auto* p = pending.exchange(nullptr))
        destroyOwned(p);
    if (current != nullptr)
    {
        for (int i = 0; i < current->numLanes; ++i)
            current->lane[static_cast<size_t>(i)]->Reset(sampleRate, maxBlockSize);
        latency.store(current->lane[0]->latency(), std::memory_order_relaxed);
    }
}

void ModelSlot::requestLoad(const std::filesystem::path& path)
{
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        requestedPath = path;
        loadRequested = true;
    }
    jobCv.notify_all();
}

void ModelSlot::requestClear()
{
    for (size_t i = 0; i < kDrivePointsDb.size(); ++i)
        riseDb[i].store(kDrivePointsDb[i], std::memory_order_relaxed);
    {
        // A parked load would resurrect the model after the clear; drop it.
        std::lock_guard<std::mutex> lock(jobMutex);
        loadRequested = false;
    }
    clearRequested.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(infoMutex);
        currentInfo = ModelInfo{};
    }
    jobCv.notify_all(); // wake worker to drain the retired pair promptly
}

void ModelSlot::setSlim(const double value)
{
    slim.store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        slimRequested = true;
    }
    jobCv.notify_all();
}

void ModelSlot::setLanes(const int numLanes)
{
    const int clamped = std::clamp(numLanes, 1, 2);
    if (desiredLanes.exchange(clamped, std::memory_order_relaxed) == clamped)
        return; // no change

    // Rebuild the loaded model at the new width (a normal load job).
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(infoMutex);
        if (!currentInfo.loaded)
            return;
        path = std::filesystem::path{currentInfo.path};
    }
    requestLoad(path);
}

ModelInfo ModelSlot::info() const
{
    std::lock_guard<std::mutex> lock(infoMutex);
    return currentInfo;
}

// --- worker thread ------------------------------------------------------------

void ModelSlot::destroyOwned(ModelPair* p)
{
    std::lock_guard<std::mutex> lock(ownedMutex);
    if (p == lastPublished)
        lastPublished = nullptr;
    owned.erase(std::remove_if(owned.begin(), owned.end(),
                               [p](const auto& u) { return u.get() == p; }),
                owned.end());
}

void ModelSlot::drainRetired()
{
    size_t tail = retireTail.load(std::memory_order_relaxed);
    while (tail != retireHead.load(std::memory_order_acquire))
    {
        ModelPair* p = retireRing[tail];
        tail = (tail + 1) % kRetireSlots;
        retireTail.store(tail, std::memory_order_release);
        destroyOwned(p);
    }
}

void ModelSlot::workerLoop()
{
    for (;;)
    {
        std::filesystem::path path;
        bool doLoad = false, doSlim = false;

        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobCv.wait_for(lock, std::chrono::milliseconds(250), [this] {
                return quitRequested || slimRequested
                       || (loadRequested && prepared.load(std::memory_order_acquire));
            });
            if (quitRequested)
                return;
            if (loadRequested && prepared.load(std::memory_order_acquire))
            {
                path = requestedPath;
                loadRequested = false;
                doLoad = true;
            }
            if (slimRequested)
            {
                slimRequested = false;
                doSlim = true;
            }
        }

        drainRetired(); // free anything the audio thread handed back

        if (doLoad)
            loadJob(path);

        if (doSlim && !doLoad) // a fresh load already applied the value
        {
            std::lock_guard<std::mutex> lock(ownedMutex);
            // lastPublished may already be retired but is alive until we
            // drain it — applying slim to it then is wasted, not unsafe.
            if (lastPublished != nullptr)
                for (int i = 0; i < lastPublished->numLanes; ++i)
                    if (auto* s = lastPublished->lane[static_cast<size_t>(i)]->slimmable())
                        s->SetSlimmableSize(slim.load(std::memory_order_relaxed));
        }
    }
}

void ModelSlot::loadJob(const std::filesystem::path& path)
{
    ModelInfo newInfo;
    newInfo.path = path.string();

    auto pair = std::make_unique<ModelPair>();
    try
    {
        const int lanes = desiredLanes.load(std::memory_order_relaxed);

        for (int i = 0; i < lanes; ++i)
        {
            // Two independent instances for stereo: the models are stateful,
            // so lanes can't share one.
            std::unique_ptr<nam::DSP> model = nam::get_dsp(path);

            if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
                throw std::runtime_error("model must be 1-in/1-out, got "
                                         + std::to_string(model->NumInputChannels()) + "-in/"
                                         + std::to_string(model->NumOutputChannels()) + "-out");

            // Build at the current engine format; if prepare() changes it
            // while we build, rebuild before publishing.
            std::unique_ptr<ResamplingNam> wrapped;
            for (;;)
            {
                const double rate = engineSampleRate.load(std::memory_order_acquire);
                const int block = engineMaxBlock.load(std::memory_order_acquire);

                if (wrapped == nullptr)
                    wrapped = std::make_unique<ResamplingNam>(std::move(model), rate, block);
                else
                    wrapped->Reset(rate, block);

                if (rate == engineSampleRate.load(std::memory_order_acquire)
                    && block == engineMaxBlock.load(std::memory_order_acquire))
                    break;
            }

            if (auto* s = wrapped->slimmable())
                s->SetSlimmableSize(slim.load(std::memory_order_relaxed));

            pair->lane[static_cast<size_t>(i)] = std::move(wrapped);
            pair->numLanes = i + 1;
        }

        newInfo.loaded = true;
        newInfo.numLanes = pair->numLanes;
        newInfo.nativeSampleRate = pair->lane[0]->encapsulatedSampleRate();
        newInfo.hasLoudness = pair->lane[0]->HasLoudness();
        newInfo.loudness = newInfo.hasLoudness ? pair->lane[0]->GetLoudness() : 0.0;
        newInfo.slimmable = pair->lane[0]->slimmable() != nullptr;
        if (auto* sl = pair->lane[0]->slimmable())
        {
            auto bps = sl->GetSlimmableSizeBreakpoints();
            std::sort(bps.begin(), bps.end());
            bps.erase(std::unique(bps.begin(), bps.end(),
                                  [](double a, double b) { return std::abs(a - b) < 1e-9; }),
                      bps.end());
            newInfo.qualityBreakpoints = std::move(bps);
        }
    }
    catch (const std::exception& e)
    {
        // Report the failure but leave whatever is currently active playing.
        std::lock_guard<std::mutex> lock(infoMutex);
        currentInfo.error = e.what();
        return;
    }

    measureRiseCurve(*pair->lane[0]);

    ModelPair* raw = pair.get();
    {
        std::lock_guard<std::mutex> lock(ownedMutex);
        owned.push_back(std::move(pair));
        lastPublished = raw;
    }

    // Publish. An unconsumed previous pending (rapid consecutive loads) is
    // ours to free — the audio thread never saw it.
    if (auto* unconsumed = pending.exchange(raw, std::memory_order_acq_rel))
        destroyOwned(unconsumed);

    {
        std::lock_guard<std::mutex> lock(infoMutex);
        currentInfo = std::move(newInfo);
    }
}

} // namespace namrig::engine
