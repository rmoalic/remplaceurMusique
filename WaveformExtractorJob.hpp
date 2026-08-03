#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <functional>
#include "WaveformExtractor.hpp"

class WaveformExtractorJob
{
public:
    using ReadyCallback = std::function<void(uint64_t generation, std::vector<float> values)>;

    explicit WaveformExtractorJob(int sampleCount = 700, int sampleRateHz = 8000);
    ~WaveformExtractorJob();

    uint64_t Start(std::wstring path, double durationSecs, ReadyCallback onReady);
    uint64_t CurrentGeneration() const {
        return m_generation.load();
    }
    void RequestStop();
    void Join();

private:
    void RunOnThread(std::wstring path, uint64_t generation, double durationSecs, ReadyCallback onReady);

    WaveformExtractor m_extractor;
    std::atomic<uint64_t> m_generation{ 0 };
    std::atomic<bool> m_stopRequested{ false };
    std::vector<std::thread> m_threads;
};