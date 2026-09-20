#include "WaveformExtractorJob.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // CoInitializeEx/CoUninitialize
#include <objbase.h>
#include <stop_token>

WaveformExtractorJob::WaveformExtractorJob(int sampleCount, int sampleRateHz)
    : m_extractor(sampleCount, sampleRateHz)
{
}

WaveformExtractorJob::~WaveformExtractorJob()
{
    RequestStop();
    Join();
}

void WaveformExtractorJob::SetInput(std::wstring path, double durationSecs, ReadyCallback onReady)
{
    std::lock_guard<std::mutex> lk(m_inputMutex);
    m_path = std::move(path);
    m_durationSecs = durationSecs;
    m_onReady = std::move(onReady);
}

void WaveformExtractorJob::run(std::stop_token stopToken, uint64_t generation)
{
    // Capture des paramètres au début de l'exécution
    std::wstring path;
    double durationSecs;
    ReadyCallback onReady;
    {
        std::lock_guard<std::mutex> lk(m_inputMutex);
        path = m_path;
        durationSecs = m_durationSecs;
        onReady = m_onReady;
    }

    if (path.empty()) return;

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coHr)) return;

    // Predicate utilisé par WaveformExtractor::Extract
    auto shouldStop = [this, generation, &stopToken]() -> bool {
        return StopRequested() || stopToken.stop_requested() || (generation != CurrentGeneration());
    };

    std::vector<float> values = m_extractor.Extract(path, durationSecs, shouldStop);

    if (!shouldStop() && onReady) {
        onReady(generation, std::move(values));
    }

    CoUninitialize();
}