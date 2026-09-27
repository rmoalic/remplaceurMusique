#pragma once
#include <string>
#include <vector>
#include <functional>

class WaveformExtractor
{
public:
    explicit WaveformExtractor(int sampleCount = 700, int sampleRateHz = 8000);

    std::vector<float> Extract(
        const std::wstring& path, double durationSecs,
        const std::function<bool()>& shouldStop) const;

    int sampleCount() const {
        return m_sampleCount;
    }
    int sampleRateHz() const {
        return m_sampleRateHz;
    }

private:
    int m_sampleCount;
    int m_sampleRateHz;
};