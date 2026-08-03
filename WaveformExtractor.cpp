#include "WaveformExtractor.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <cstdint>
#include <cwchar> // swprintf_s

using Microsoft::WRL::ComPtr;

static void LogWaveform(const wchar_t* msg)
{
    OutputDebugStringW(L"WaveformExtractor: ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
}

WaveformExtractor::WaveformExtractor(int sampleCount, int sampleRateHz)
    : m_sampleCount(sampleCount)
    , m_sampleRateHz(sampleRateHz)
{
}

std::vector<float> WaveformExtractor::Extract(
    const std::wstring& path, double durationSecs,
    const std::function<bool()>& shouldStop) const
{
    if (m_sampleCount <= 0) {
        LogWaveform(L"invalid sampleCount <= 0");
        return {};
    }
    if (m_sampleRateHz <= 0) {
        LogWaveform(L"invalid sampleRateHz <= 0");
        return {};
    }

    ComPtr<IMFSourceReader> pR;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &pR);
    if (FAILED(hr)) {
        wchar_t buf[256];
        swprintf_s(buf, L"MFCreateSourceReaderFromURL failed: 0x%08X", static_cast<UINT32>(hr));
        LogWaveform(buf);
        return {};
    }

    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> pT;
    hr = MFCreateMediaType(&pT);
    if (FAILED(hr) || !pT) {
        wchar_t buf[256];
        swprintf_s(buf, L"MFCreateMediaType failed: 0x%08X", static_cast<UINT32>(hr));
        LogWaveform(buf);
        return {};
    }
    pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
    pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(m_sampleRateHz));
    pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr = pR->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT.Get());
    if (FAILED(hr)) {
        wchar_t buf[256];
        swprintf_s(buf, L"SetCurrentMediaType failed: 0x%08X", static_cast<UINT32>(hr));
        LogWaveform(buf);
        return {};
    }

    std::vector<float> values(static_cast<size_t>(m_sampleCount), 0.f);
    const uint64_t totalSamples = durationSecs > 0.0
        ? static_cast<uint64_t>(durationSecs * static_cast<double>(m_sampleRateHz)) : 0ULL;
    const uint64_t samplesPerBucket = totalSamples > 0
        ? (std::max<uint64_t>)(1ULL, (totalSamples + static_cast<uint64_t>(m_sampleCount) - 1ULL) / static_cast<uint64_t>(m_sampleCount))
        : 1ULL;
    uint64_t sampleIndex = 0ULL;
    const size_t bucketCount = static_cast<size_t>(m_sampleCount);

    while (true) {
        if (shouldStop && shouldStop()) {
            LogWaveform(L"extraction cancelled by shouldStop");
            return {};
        }

        ComPtr<IMFSample> pS;
        DWORD flags = 0;
        hr = pR->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &pS);
        if (FAILED(hr)) {
            wchar_t buf[256];
            swprintf_s(buf, L"ReadSample failed: 0x%08X", static_cast<UINT32>(hr));
            LogWaveform(buf);
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!pS) continue;

        ComPtr<IMFMediaBuffer> pB;
        hr = pS->ConvertToContiguousBuffer(&pB);
        if (FAILED(hr) || !pB) {
            wchar_t buf[256];
            swprintf_s(buf, L"ConvertToContiguousBuffer failed: 0x%08X", static_cast<UINT32>(hr));
            LogWaveform(buf);
            continue;
        }

        BYTE* d = nullptr;
        DWORD len = 0;
        hr = pB->Lock(&d, nullptr, &len);
        if (FAILED(hr) || !d) {
            wchar_t buf[256];
            swprintf_s(buf, L"MediaBuffer::Lock failed: 0x%08X", static_cast<UINT32>(hr));
            LogWaveform(buf);
            continue;
        }

        if ((len & 1) != 0) {
            wchar_t buf[256];
            swprintf_s(buf, L"odd buffer length encountered (%u), trimming last byte", (unsigned)len);
            LogWaveform(buf);
            --len;
        }

        const int n = static_cast<int>(len / 2);
        auto* s16 = reinterpret_cast<int16_t*>(d);
        for (int i = 0; i < n; ++i, ++sampleIndex) {
            const size_t bucket = std::min<size_t>(bucketCount - 1, static_cast<size_t>(sampleIndex / samplesPerBucket));
            const float amplitude = std::abs(static_cast<float>(s16[i])) / 32768.f;
            values[bucket] = std::max<float>(values[bucket], std::min<float>(1.f, amplitude));
        }

        pB->Unlock();
    }

    return values;
}