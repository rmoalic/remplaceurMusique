#include "MainViewModel.hpp"
#include <utility>

MainViewModel::MainViewModel(int sampleCount)
{
    m_waveJob = std::make_unique<WaveformExtractorJob>(sampleCount);
}

MainViewModel::~MainViewModel()
{
    // Demander l'arrêt des jobs et laisser les destructeurs faire Join() proprement.
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_waveJob) m_waveJob->RequestStop();
        if (m_encodeJob) m_encodeJob->RequestStop();
    }
    // reset (définira les destructeurs qui joindront)
    m_waveJob.reset();
    m_encodeJob.reset();
}

void MainViewModel::SetAudioPath(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_audioPath = path;
}

std::wstring MainViewModel::GetAudioPath()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_audioPath;
}

uint64_t MainViewModel::StartWaveformExtraction(double durationSecs)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_waveJob) m_waveJob = std::make_unique<WaveformExtractorJob>();
    // Prépare le callback ready (appelé depuis le worker thread)
    auto ready = [this](uint64_t generation, std::vector<float> values) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            WaveformResult wr{ generation, std::move(values) };
            m_results.emplace(generation, std::move(wr));
        }
        // notifier la vue (worker -> UI doit marshaller)
        GenCallback cb;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            cb = m_onGenReady;
        }
        if (cb) cb(generation);
    };
    // copier path pour SetInput
    std::wstring pathCopy;
    {
        pathCopy = m_audioPath;
    }
    m_waveJob->SetInput(pathCopy, durationSecs, ready);
    m_waveJob->Start();
    return m_waveJob->CurrentGeneration();
}

std::unique_ptr<WaveformResult> MainViewModel::ConsumeWaveform(uint64_t generation)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_results.find(generation);
    if (it == m_results.end()) return nullptr;
    auto out = std::make_unique<WaveformResult>();
    out->generation = it->second.generation;
    out->values = std::move(it->second.values);
    m_results.erase(it);
    return out;
}

void MainViewModel::SetOnGenerationReady(GenCallback cb)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_onGenReady = std::move(cb);
}

/* --- Propriétés vidéo/audio simples --- */
void MainViewModel::SetVideoPath(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_videoPath = path;
}

std::wstring MainViewModel::GetVideoPath()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_videoPath;
}

void MainViewModel::SetVideoRange(double startSec, double endSec)
{
    (void)startSec; (void)endSec; // placeholder si besoin futur
}
void MainViewModel::SetAudioRange(double startSec, double endSec)
{
    (void)startSec; (void)endSec;
}
void MainViewModel::SetQualityIndex(int idx)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_qualityIdx = idx;
}
void MainViewModel::SetVolumeScale(float scale)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_volumeScale = scale;
}

/* --- Encodage --- */
uint64_t MainViewModel::StartEncode(std::unique_ptr<EncodeParams> params)
{
    if (!params) return 0;
    std::lock_guard<std::mutex> lk(m_mtx);
    // Si un job existe, demander son arrêt (on ne supprime pas tout de suite pour éviter de Join() depuis worker)
    if (m_encodeJob) {
        m_encodeJob->RequestStop();
    }

    EncodeCallbacks cbs;
    cbs.onProgress = [this](int pct, double etaSecs) {
        EncodeProgressCb cb;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            cb = m_onEncodeProgress;
        }
        if (cb) cb(pct, etaSecs);
    };
    cbs.onDone = [this](bool ok, EncodeErrorInfo info) {
        EncodeDoneCb cb;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            cb = m_onEncodeDone;
        }
        if (cb) cb(ok, info);
        // Ne pas libérer m_encodeJob ici : le callback est exécuté depuis le worker thread
        // et la suppression ferait Join() sur le thread courant -> deadlock.
        // L'objet restera jusqu'à ce qu'un nouvel StartEncode ou le destructeur le nettoie.
    };

    m_encodeJob = std::make_unique<EncodeJob>(std::move(params), std::move(cbs));
    m_encodeJob->Start();
    return m_encodeJob->CurrentGeneration();
}

void MainViewModel::CancelEncode()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_encodeJob) m_encodeJob->RequestStop();
}

void MainViewModel::SetOnEncodeProgress(EncodeProgressCb cb)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_onEncodeProgress = std::move(cb);
}
void MainViewModel::SetOnEncodeDone(EncodeDoneCb cb)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_onEncodeDone = std::move(cb);
}