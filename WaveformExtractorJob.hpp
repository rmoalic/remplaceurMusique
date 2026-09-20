#pragma once
#include "AbstractJob.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include "WaveformExtractor.hpp"

class WaveformExtractorJob : public AbstractJob
{
public:
    using ReadyCallback = std::function<void(uint64_t generation, std::vector<float> values)>;

    explicit WaveformExtractorJob(int sampleCount = 700, int sampleRateHz = 8000);
    ~WaveformExtractorJob() override;

    // Prépare la prochaine exécution (doit être appelé avant Start())
    void SetInput(std::wstring path, double durationSecs, ReadyCallback onReady);

protected:
    void run(std::stop_token stopToken, uint64_t generation) override;

private:
    WaveformExtractor m_extractor;

    // paramètres de la prochaine exécution (protégés par mutex)
    std::wstring m_path;
    double m_durationSecs{ 0.0 };
    ReadyCallback m_onReady;
    std::mutex m_inputMutex;
};