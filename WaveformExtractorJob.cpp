#include "WaveformExtractorJob.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // CoInitializeEx/CoUninitialize only
#include <objbase.h>   // définit CoInitializeEx, CoUninitialize, COINIT_* constantes

WaveformExtractorJob::WaveformExtractorJob(int sampleCount, int sampleRateHz)
    : m_extractor(sampleCount, sampleRateHz)
{
}

WaveformExtractorJob::~WaveformExtractorJob()
{
    RequestStop();
    Join();
}

uint64_t WaveformExtractorJob::Start(std::wstring path, double durationSecs, ReadyCallback onReady)
{
    const uint64_t generation = ++m_generation;
    m_threads.emplace_back(
    [this, p = std::move(path), generation, durationSecs, cb = std::move(onReady)]() mutable {
        const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(coHr)) return;
        RunOnThread(std::move(p), generation, durationSecs, std::move(cb));
        CoUninitialize();
    });
    return generation;
}

void WaveformExtractorJob::RequestStop()
{
    m_stopRequested = true;
}

void WaveformExtractorJob::Join()
{
    for (auto& t : m_threads)
        if (t.joinable()) t.join();
    m_threads.clear();
}

void WaveformExtractorJob::RunOnThread(
    std::wstring path, uint64_t generation, double durationSecs, ReadyCallback onReady)
{
    // Checked both inside WaveformExtractor::Extract (to abort mid-decode)
    // and again here after it returns (to avoid firing the callback for a
    // request that was superseded/shut down while Extract() was finishing
    // its last buffer).
    auto shouldStop = [this, generation] {
        return m_stopRequested.load() || generation != m_generation.load();
    };

    if (shouldStop()) return;

    std::vector<float> values = m_extractor.Extract(path, durationSecs, shouldStop);

    if (shouldStop()) return;

    if (onReady)
        onReady(generation, std::move(values));
}