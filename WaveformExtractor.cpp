#include "WaveformExtractor.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <cstdint>

using Microsoft::WRL::ComPtr;

WaveformExtractor::WaveformExtractor(int sampleCount, int sampleRateHz)
    : m_sampleCount(sampleCount)
    , m_sampleRateHz(sampleRateHz)
{
}

std::vector<float> WaveformExtractor::Extract(
    const std::wstring& path, double durationSecs,
    const std::function<bool()>& shouldStop) const
{
    ComPtr<IMFSourceReader> pR;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &pR)))
        return {};

    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> pT;
    MFCreateMediaType(&pT);
    pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
    pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_sampleRateHz);
    pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (FAILED(pR->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT.Get())))
        return {};

    std::vector<float> values((size_t)m_sampleCount, 0.f);
    const uint64_t totalSamples = durationSecs > 0.0
                                  ? static_cast<uint64_t>(durationSecs * m_sampleRateHz) : 0;
    const uint64_t samplesPerBucket = totalSamples > 0
                                      ? std::max<uint64_t>(1, (totalSamples + m_sampleCount - 1) / m_sampleCount) : 1;
    uint64_t sampleIndex = 0;

    while (true) {
        if (shouldStop && shouldStop()) return {};

        ComPtr<IMFSample> pS;
        DWORD flags = 0;
        HRESULT hr = pR->ReadSample(
                         (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &pS);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
        if (!pS) continue;

        ComPtr<IMFMediaBuffer> pB;
        if (SUCCEEDED(pS->ConvertToContiguousBuffer(&pB))) {
            BYTE* d = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(pB->Lock(&d, nullptr, &len))) {
                int n = len / 2;
                auto* s16 = reinterpret_cast<int16_t*>(d);
                for (int i = 0; i < n; i++, sampleIndex++) {
                    const size_t bucket = std::min<size_t>((size_t)m_sampleCount - 1, sampleIndex / samplesPerBucket);
                    const float amplitude = std::abs((float)s16[i]) / 32768.f;
                    values[bucket] = std::max<float>(values[bucket], std::min<float>(1.f, amplitude));
                }
                pB->Unlock();
            }
        }
    }

    return values;
}