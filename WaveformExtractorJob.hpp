#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include "WaveformExtractor.hpp"

#define WM_WAVEFORM_READY   (WM_USER+2)

struct WaveformResult
{
    uint64_t generation;
    std::vector<float> values;
};

class WaveformExtractorJob
{
public:
    explicit WaveformExtractorJob(int sampleCount = 700, int sampleRateHz = 8000);
    ~WaveformExtractorJob();

    uint64_t Start(std::wstring path, HWND hWnd, double durationSecs);
    uint64_t CurrentGeneration() const {
        return m_generation.load();
    }
    void RequestStop();
    void Join();

private:
    void RunOnThread(std::wstring path, HWND hWnd, uint64_t generation, double durationSecs);

    WaveformExtractor m_extractor;
    std::atomic<uint64_t> m_generation{ 0 };
    std::atomic<bool> m_stopRequested{ false };
    std::vector<std::thread> m_threads;
};
