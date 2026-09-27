#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include "WaveformExtractorJob.hpp"
#include "Encode.hpp"
#include "EncodeJob.hpp"

struct WaveformResult {
    uint64_t generation;
    std::vector<float> values;
};

class MainViewModel {
public:
    using GenCallback = std::function<void(uint64_t)>; // appelé depuis le worker thread : la View doit marshaller
    using EncodeProgressCb = std::function<void(int pct, double etaSecs)>;
    using EncodeDoneCb     = std::function<void(bool ok, EncodeErrorInfo error)>;

    explicit MainViewModel(int sampleCount = 700);
    ~MainViewModel();

    // Waveform
    void SetAudioPath(const std::wstring& path);
    std::wstring GetAudioPath();
    uint64_t StartWaveformExtraction(double durationSecs);
    std::unique_ptr<WaveformResult> ConsumeWaveform(uint64_t generation);
    void SetOnGenerationReady(GenCallback cb);

    // Propriétés vidéo/audio (thread-safe)
    void SetVideoPath(const std::wstring& path);
    std::wstring GetVideoPath();

    void SetVideoRange(double startSec, double endSec);
    void SetAudioRange(double startSec, double endSec);

    void SetQualityIndex(int idx);
    void SetVolumeScale(float scale);

    // Encodage
    // Démarre une tâche d'encodage asynchrone. Retourne la génération du job (0 si non démarré).
    uint64_t StartEncode(std::unique_ptr<EncodeParams> params);
    void CancelEncode();

    void SetOnEncodeProgress(EncodeProgressCb cb);
    void SetOnEncodeDone(EncodeDoneCb cb);

private:
    std::mutex m_mtx;

    // audio/waveform
    std::wstring m_audioPath;
    std::unique_ptr<WaveformExtractorJob> m_waveJob;
    std::unordered_map<uint64_t, WaveformResult> m_results; // protégés par m_mtx
    GenCallback m_onGenReady;

    // encode
    std::wstring m_videoPath;
    int m_qualityIdx{ 0 };
    float m_volumeScale{ 1.0f };

    std::unique_ptr<EncodeJob> m_encodeJob;
    EncodeProgressCb m_onEncodeProgress;
    EncodeDoneCb     m_onEncodeDone;
};