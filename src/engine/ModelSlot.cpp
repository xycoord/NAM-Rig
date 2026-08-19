#include "ModelSlot.h"

#include <algorithm>
#include <chrono>

#include "NAM/get_dsp.h"

#include "Architectures.h"

namespace namrig::engine
{

ModelSlot::ModelSlot()
{
    registerBuiltinArchitectures();
    worker = std::thread([this] { workerLoop(); });
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

bool ModelSlot::pushRetired(ResamplingNam* p)
{
    const size_t head = retireHead.load(std::memory_order_relaxed);
    const size_t next = (head + 1) % kRetireSlots;
    if (next == retireTail.load(std::memory_order_acquire))
        return false; // full — caller keeps things as they are this block
    retireRing[head] = p;
    retireHead.store(next, std::memory_order_release);
    return true;
}

ResamplingNam* ModelSlot::render()
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

    if (ResamplingNam* incoming = pending.exchange(nullptr, std::memory_order_acq_rel))
    {
        if (current == nullptr || pushRetired(current))
        {
            current = incoming;
            latency.store(current->latency(), std::memory_order_relaxed);
        }
        else
        {
            // Retire ring full (pathological). Defer the swap: put the new
            // model back and try again next block.
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

    // Audio is stopped (prepareToPlay contract): the current model and the
    // pending slot are safe to touch directly.
    if (auto* p = pending.exchange(nullptr))
        destroyOwned(p);
    if (current != nullptr)
    {
        current->Reset(sampleRate, maxBlockSize); // includes prewarm
        latency.store(current->latency(), std::memory_order_relaxed);
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
    jobCv.notify_all(); // wake worker to drain the retired model promptly
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

ModelInfo ModelSlot::info() const
{
    std::lock_guard<std::mutex> lock(infoMutex);
    return currentInfo;
}

// --- worker thread ------------------------------------------------------------

void ModelSlot::destroyOwned(ResamplingNam* p)
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
        ResamplingNam* p = retireRing[tail];
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
                if (auto* s = lastPublished->slimmable())
                    s->SetSlimmableSize(slim.load(std::memory_order_relaxed));
        }
    }
}

void ModelSlot::loadJob(const std::filesystem::path& path)
{
    ModelInfo newInfo;
    newInfo.path = path.string();

    std::unique_ptr<ResamplingNam> wrapped;
    try
    {
        std::unique_ptr<nam::DSP> model = nam::get_dsp(path);

        if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
            throw std::runtime_error("model must be 1-in/1-out, got "
                                     + std::to_string(model->NumInputChannels()) + "-in/"
                                     + std::to_string(model->NumOutputChannels()) + "-out");

        // Build at the current engine format; if prepare() changes it while
        // we build, rebuild before publishing.
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

        newInfo.loaded = true;
        newInfo.nativeSampleRate = wrapped->encapsulatedSampleRate();
        newInfo.hasLoudness = wrapped->HasLoudness();
        newInfo.loudness = newInfo.hasLoudness ? wrapped->GetLoudness() : 0.0;
        newInfo.slimmable = wrapped->slimmable() != nullptr;
    }
    catch (const std::exception& e)
    {
        // Report the failure but leave whatever is currently active playing.
        std::lock_guard<std::mutex> lock(infoMutex);
        currentInfo.error = e.what();
        return;
    }

    ResamplingNam* raw = wrapped.get();
    {
        std::lock_guard<std::mutex> lock(ownedMutex);
        owned.push_back(std::move(wrapped));
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
