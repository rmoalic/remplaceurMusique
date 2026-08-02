#include "WaveformExtractorJob.hpp"

WaveformExtractorJob::WaveformExtractorJob(int sampleCount, int sampleRateHz)
    : m_extractor(sampleCount, sampleRateHz)
{
}

WaveformExtractorJob::~WaveformExtractorJob()
{
    RequestStop();
    Join();
}

uint64_t WaveformExtractorJob::Start(std::wstring path, HWND hWnd, double durationSecs)
{
    const uint64_t generation = ++m_generation;
    m_threads.emplace_back([this, p = std::move(path), hWnd, generation, durationSecs] {
        const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(coHr)) return;
        RunOnThread(p, hWnd, generation, durationSecs);
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

void WaveformExtractorJob::RunOnThread(std::wstring path, HWND hWnd, uint64_t generation, double durationSecs)
{
    auto shouldStop = [this, generation] {
        return m_stopRequested.load() || generation != m_generation.load();
    };

    if (shouldStop()) return;

    std::vector<float> values = m_extractor.Extract(path, durationSecs, shouldStop);

    if (shouldStop()) return;

    if (values.empty()) {
        PostMessage(hWnd, WM_WAVEFORM_READY, (WPARAM)generation, 0);
        return;
    }

    auto* result = new WaveformResult{ generation, std::move(values) };
    if (!PostMessage(hWnd, WM_WAVEFORM_READY, (WPARAM)generation, (LPARAM)result))
        delete result;
}
