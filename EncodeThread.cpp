#include <memory>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl/client.h>
#include <d3d11.h>
#include <d3d11_4.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <mfobjects.h>

using namespace Microsoft::WRL;

#include "EncodeThread.hpp"
#include "Encode.hpp"

class EncodeThread {
    ComPtr<ID3D11Device>         pD3DDevice;
    ComPtr<ID3D11DeviceContext>  pD3DContext;
    ComPtr<IMFDXGIDeviceManager> pDevMgr;
    
    EncodeThread(std::unique_ptr<EncodeParams> params, QualityPreset preset) {
        init_D3D11_DXGI();
        ComPtr<IMFSourceReader> pVideo = init_video_source_reader(params->videoPath);
        ComPtr<IMFSourceReader> pAudio = init_audio_reader(params->audioPath, params->audioStart, preset.audChannels);
    }

    void Fail(std::wstring f) {
    }

    void init_D3D11_DXGI() {
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION,
            &this->pD3DDevice, nullptr, &this->pD3DContext);
        UINT resetToken = 0;
        if (SUCCEEDED(hr)) {
            ComPtr<ID3D11Multithread> pMT;
            if (SUCCEEDED(pD3DDevice->QueryInterface(IID_PPV_ARGS(&pMT))))
                pMT->SetMultithreadProtected(TRUE);

            if (SUCCEEDED(MFCreateDXGIDeviceManager(&resetToken, &this->pDevMgr)))
                this->pDevMgr->ResetDevice(pD3DDevice.Get(), resetToken);
        }
    }

    ComPtr<IMFSourceReader> init_video_source_reader(const std::wstring& video_url) {
        ComPtr<IMFSourceReader> pVid;
        {
            ComPtr<IMFAttributes> pA;
            MFCreateAttributes(&pA, 3);
            pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            pA->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
            if (pDevMgr)
                pA->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, pDevMgr.Get());

            if (FAILED(MFCreateSourceReaderFromURL(video_url.c_str(), pA.Get(), &pVid))) {
                Fail(L"Impossible d'ouvrir la vid\u00e9o source.");
                return nullptr;
            }
        }

        pVid->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        pVid->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

        // Try GPU-compatible subtypes: NV12 first (best for HW encode), then P010, YUY2
        {
            ComPtr<IMFMediaType> pDecT;
            MFCreateMediaType(&pDecT);
            pDecT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

            const GUID subtypes[] = { MFVideoFormat_NV12, MFVideoFormat_P010, MFVideoFormat_YUY2 };
            HRESULT hr = E_FAIL;
            for (const GUID& st : subtypes) {
                pDecT->SetGUID(MF_MT_SUBTYPE, st);
                hr = pVid->SetCurrentMediaType(
                    (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pDecT.Get());
                if (SUCCEEDED(hr)) break;
            }
            if (FAILED(hr)) {
                Fail(L"Erreur lecture type vid\u00e9o.");
                return nullptr;
            }
        }

        ComPtr<IMFMediaType> pVidActual;
        pVid->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pVidActual);
        if (!pVidActual) {
            Fail(L"Erreur lecture type vid\u00e9o.");
            return nullptr;
        }

        UINT32 vidW = 0, vidH = 0;
        MFGetAttributeSize(pVidActual.Get(), MF_MT_FRAME_SIZE, &vidW, &vidH);
        UINT32 frNum = 0, frDen = 1;
        MFGetAttributeRatio(pVidActual.Get(), MF_MT_FRAME_RATE, &frNum, &frDen);
        if (frNum == 0) {
            frNum = 30;
            frDen = 1;
        }
        return pVid;
    }

    ComPtr<IMFSourceReader> init_audio_reader(const std::wstring& path, double seekSecs, int channels)
    {
        ComPtr<IMFSourceReader> pR;
        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &pR))) return {};

        pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

        ComPtr<IMFMediaType> pT;
        MFCreateMediaType(&pT);
        pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
        pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
        pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (FAILED(pR->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT.Get())))
            return {};

        if (seekSecs > 0.0) {
            PROPVARIANT v;
            v.vt = VT_I8;
            v.hVal.QuadPart = (LONGLONG)(seekSecs * 1e7);
            pR->SetCurrentPosition(GUID_NULL, v);
        }
        if (!pR) {
            Fail(L"Impossible de d\u00e9coder l'audio.");
            return nullptr;
        }
        return pR;
    }

    ComPtr<IMFSinkWriter> init_sink_writer() {
        ComPtr<IMFSinkWriter> pW;
        ComPtr<IMFAttributes> pA;
        MFCreateAttributes(&pA, 3);
        pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        pA->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (pDevMgr)
            pA->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, pDevMgr.Get());

        if (FAILED(MFCreateSinkWriterFromURL(
            params->outputPath.c_str(), nullptr, pA.Get(), &pW))) {
            Fail(L"Impossible de cr\u00e9er le fichier de sortie.");
            return nullptr;
        }
        return pW;
    }

    void init_video_output_stream(preset) {
        UINT32 outW = vidW, outH = vidH;

        // Swap the preset bounding box for portrait videos so the limit that
        // was intended for width applies to height and vice-versa.
        UINT32 maxW = preset.maxWidth, maxH = preset.maxHeight;
        if (vidH > vidW && maxW > 0 && maxH > 0) std::swap(maxW, maxH);

        if (maxW > 0 || maxH > 0) {
            float scaleW = (maxW > 0 && outW > maxW) ? (float)maxW / outW : 1.0f;
            float scaleH = (maxH > 0 && outH > maxH) ? (float)maxH / outH : 1.0f;
            float scale = min(scaleW, scaleH);
            if (scale < 1.0f) {
                outW = (UINT32)(outW * scale);
                outH = (UINT32)(outH * scale);
            }
        }
        // H.264 requires even dimensions
        outW = (outW & ~1u);
        if (outW == 0) outW = 2;
        outH = (outH & ~1u);
        if (outH == 0) outH = 2;

        UINT32 srcBitrate = MF_GetVideoBitrate(params->videoPath);
        UINT32 vBitrate = (preset.maxVidBitrate == 0)
            ? ((srcBitrate > 0) ? srcBitrate : 4000000)
            : ((srcBitrate > 0) ? min(preset.maxVidBitrate, srcBitrate) : preset.maxVidBitrate);

        DWORD vidIdx = 0, audIdx = 1;

        {
            ComPtr<IMFMediaType> pVidOut;
            MFCreateMediaType(&pVidOut);
            pVidOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pVidOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
            pVidOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize(pVidOut.Get(), MF_MT_FRAME_SIZE, outW, outH);
            MFSetAttributeRatio(pVidOut.Get(), MF_MT_FRAME_RATE, frNum, frDen);
            MFSetAttributeRatio(pVidOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            pVidOut->SetUINT32(MF_MT_AVG_BITRATE, vBitrate);
            pVidOut->SetUINT32(MF_MT_MPEG2_PROFILE, (UINT32)preset.h264Profile);

            if (FAILED(pW->AddStream(pVidOut.Get(), &vidIdx)))
                return Fail(L"Erreur ajout flux H264.");
        }
        if (FAILED(pW->SetInputMediaType(vidIdx, pVidActual.Get(), nullptr)))
            return Fail(L"Type vid\u00e9o incompatible avec l'encodeur H264.");

    }
};

static LONGLONG WriteSilenceChunk(
    IMFSinkWriter* pW, DWORD audIdx,
    LONGLONG ts, LONGLONG durationHns,
    LONGLONG bytesPerHns_num, LONGLONG bytesPerHns_den)
{
    if (durationHns <= 0) return 0;
    LONGLONG nbytes = (durationHns * bytesPerHns_num + bytesPerHns_den - 1) / bytesPerHns_den;
    nbytes = (nbytes + 3) & ~3LL;
    const LONGLONG MAX_BYTES = 1024 * 1024;
    if (nbytes > MAX_BYTES) {
        nbytes = MAX_BYTES;
        nbytes = (nbytes + 3) & ~3LL;
    }
    LONGLONG realHns = nbytes * bytesPerHns_den / bytesPerHns_num;
    if (realHns <= 0) return 0;

    ComPtr<IMFMediaBuffer> pBuf;
    if (FAILED(MFCreateMemoryBuffer((DWORD)nbytes, &pBuf))) return 0;
    BYTE* d = nullptr;
    DWORD mL = 0;
    pBuf->Lock(&d, &mL, nullptr);
    memset(d, 0, (size_t)nbytes);
    pBuf->Unlock();
    pBuf->SetCurrentLength((DWORD)nbytes);

    ComPtr<IMFSample> pS;
    MFCreateSample(&pS);
    pS->AddBuffer(pBuf.Get());
    pS->SetSampleTime(ts);
    pS->SetSampleDuration(realHns);
    pW->WriteSample(audIdx, pS.Get());
    return realHns;
}

double MF_GetDuration(const std::wstring& path)
{
    ComPtr<IMFSourceReader> r;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &r))) return 0.0;
    PROPVARIANT v;
    PropVariantInit(&v);
    double d = 0;
    if (SUCCEEDED(r->GetPresentationAttribute(
                      (DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8)
        d = (double)v.uhVal.QuadPart / 1e7;
    PropVariantClear(&v);
    return d;
}

UINT32 MF_GetVideoBitrate(const std::wstring& path)
{
    ComPtr<IMFSourceReader> r;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &r))) return 0;
    ComPtr<IMFMediaType> pT;
    UINT32 br = 0;
    if (SUCCEEDED(r->GetNativeMediaType(
                      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pT)))
        pT->GetUINT32(MF_MT_AVG_BITRATE, &br);
    return br;
}


static void WriteAudioSample(
    IMFSinkWriter* pW, DWORD audIdx,
    IMFSample* pSrc, LONGLONG ts, float vol)
{
    if (vol == 1.0f) {
        pSrc->SetSampleTime(ts);
        pW->WriteSample(audIdx, pSrc);
        return;
    }
    ComPtr<IMFMediaBuffer> pIn;
    if (FAILED(pSrc->ConvertToContiguousBuffer(&pIn))) return;

    BYTE* pInD = nullptr;
    DWORD lenIn = 0;
    pIn->Lock(&pInD, nullptr, &lenIn);

    ComPtr<IMFMediaBuffer> pOut;
    MFCreateMemoryBuffer(lenIn, &pOut);
    BYTE* pOutD = nullptr;
    DWORD mxO = 0;
    pOut->Lock(&pOutD, &mxO, nullptr);

    auto* s = (int16_t*)pInD;
    auto* d = (int16_t*)pOutD;
    int n = lenIn / 2;
    for (int i = 0; i < n; i++) {
        float v = s[i] * vol;
        d[i] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
    }

    pOut->Unlock();
    pOut->SetCurrentLength(lenIn);
    pIn->Unlock();

    ComPtr<IMFSample> pDst;
    MFCreateSample(&pDst);
    pDst->AddBuffer(pOut.Get());
    LONGLONG dur = 0;
    pSrc->GetSampleDuration(&dur);
    pDst->SetSampleTime(ts);
    pDst->SetSampleDuration(dur);
    pW->WriteSample(audIdx, pDst.Get());
}

static void EncodeThread(std::unique_ptr<EncodeParams> params)
{
    const QualityPreset& preset = PRESETS[params->qualityIdx];
    const LONGLONG pcmBytesNum = (LONGLONG)(44100 * preset.audChannels * 2);
    const LONGLONG pcmHnsDen = 10000000LL;
    const LONGLONG audioStartHns = (LONGLONG)(params->audioStart * 1e7);
    const LONGLONG audioEndHns = (params->audioEnd > params->audioStart && params->audioEnd > 0)
                                 ? (LONGLONG)(params->audioEnd * 1e7) : 0;
    const LONGLONG audioRangeHns = (audioEndHns > 0)
                                   ? (audioEndHns - audioStartHns) : LLONG_MAX;

    // Failure helper — sets error, posts done message, returns.
    // ComPtr members clean up automatically when the function exits.
    auto Fail = [&](const wchar_t* msg) {
        //TODO: error message
        DeleteFile(params->outputPath.c_str());
        PostMessage(params->hWnd, WM_ENCODE_DONE, 0, 0);
    };

    // ── D3D11 device + DXGI device manager ──────────────────────────────────


    {

    // ── 1. Video source reader ───────────────────────────────────────────────
    // ── 2. Audio source reader ───────────────────────────────────────────────
    LONGLONG audPosInRange = 0;

    // ── 3. Sink writer ───────────────────────────────────────────────────────

    // ── 4. Video output stream (H.264) ───────────────────────────────────────

    // ── 5. Audio output stream (AAC-LC) ─────────────────────────────────────
    {
        ComPtr<IMFMediaType> pAudOut;
        MFCreateMediaType(&pAudOut);
        pAudOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pAudOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        pAudOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        pAudOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
        pAudOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)preset.audChannels);
        pAudOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, preset.audBytesPerSec);
        pAudOut->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29); // AAC-LC

        if (FAILED(pW->AddStream(pAudOut.Get(), &audIdx)))
            return Fail(L"Erreur ajout flux AAC.");
    }
    {
        ComPtr<IMFMediaType> pAudIn;
        MFCreateMediaType(&pAudIn);
        pAudIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pAudIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pAudIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        pAudIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
        pAudIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)preset.audChannels);

        if (FAILED(pW->SetInputMediaType(audIdx, pAudIn.Get(), nullptr)))
            return Fail(L"Erreur configuration AAC. (Windows 7+)");
    }

    // ── 6. Seek video to start ───────────────────────────────────────────────
    if (params->videoStart > 0.0) {
        PROPVARIANT v;
        v.vt = VT_I8;
        v.hVal.QuadPart = (LONGLONG)(params->videoStart * 1e7);
        pVid->SetCurrentPosition(GUID_NULL, v);
    }

    // ── 7. Encode loop ───────────────────────────────────────────────────────
    if (FAILED(pW->BeginWriting()))
        return Fail(L"Erreur d\u00e9marrage \u00e9criture MP4.");

    const LONGLONG maxDurHns = (params->videoEnd > params->videoStart && params->videoEnd > 0.0)
                               ? (LONGLONG)((params->videoEnd - params->videoStart) * 1e7) : LLONG_MAX;

    LONGLONG vidTimeBase = -1, vidLastTs = 0, audWritten = 0;
    bool vidDone = false, audEOF = false;
    LONGLONG lastProgressHns = 0;
    int nullStreak = 0;

    LARGE_INTEGER qpcFreq, qpcStart;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);

    while (!vidDone) {

        // Read one video frame
        {
            ComPtr<IMFSample> pS;
            DWORD flags = 0;
            LONGLONG ts = 0;
            HRESULT hr = pVid->ReadSample(
                             (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                             0, nullptr, &flags, &ts, &pS);

            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                vidDone = true;
            }
            else if (pS) {
                nullStreak = 0;
                if (vidTimeBase < 0) vidTimeBase = ts;
                LONGLONG rel = ts - vidTimeBase;

                if (maxDurHns != LLONG_MAX && rel >= maxDurHns) {
                    vidDone = true;
                }
                else {
                    pS->SetSampleTime(rel);
                    pW->WriteSample(vidIdx, pS.Get());
                    vidLastTs = rel;

                    if (maxDurHns != LLONG_MAX && rel - lastProgressHns > 5000000LL) {
                        lastProgressHns = rel;
                        int pct = (int)min(99LL, rel * 100 / maxDurHns);

                        LARGE_INTEGER qpcNow;
                        QueryPerformanceCounter(&qpcNow);
                        double elapsed = (double)(qpcNow.QuadPart - qpcStart.QuadPart)
                                         / qpcFreq.QuadPart;
                        double ratio = (elapsed > 0.0 && rel > 0)
                                       ? (double)rel / (elapsed * 1e7) : 1.0;
                        double eta = ((maxDurHns - rel) / 1e7) / ratio;

                        PostMessage(params->hWnd, WM_ENCODE_PROGRESS,
                                    (WPARAM)pct, (LPARAM)(LONGLONG)eta);
                    }
                }
            }
            else {
                if (++nullStreak > 200) vidDone = true;
            }
        }

        // Write audio to keep ~200ms ahead of video
        const LONGLONG audioTarget = min(
                                         vidLastTs + 2000000LL,
                                         maxDurHns == LLONG_MAX ? vidLastTs + 2000000LL : maxDurHns);

        while (audWritten < audioTarget) {
            if (!audEOF) {
                ComPtr<IMFSample> pS;
                DWORD flags = 0;
                HRESULT hr = pAud->ReadSample(
                                 (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                 0, nullptr, &flags, nullptr, &pS);

                bool eof = FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM);

                if (pS && !eof) {
                    LONGLONG dur = 0;
                    if (FAILED(pS->GetSampleDuration(&dur)) || dur <= 0) dur = 200000;

                    if (audioRangeHns != LLONG_MAX && audPosInRange + dur > audioRangeHns) {
                        eof = true;
                    }
                    else {
                        WriteAudioSample(pW.Get(), audIdx, pS.Get(), audWritten, params->volumeScale);
                        audWritten += dur;
                        audPosInRange += dur;
                    }
                }
                else {
                    eof = true;
                }

                if (eof) {
                    audEOF = true;
                    if (params->audioShortMode == ASM_LOOP) {
                        pAud = OpenAudioReader(
                                   params->audioPath, params->audioStart, preset.audChannels);
                        audPosInRange = 0;
                        if (pAud) audEOF = false;
                    }
                }
            }
            else {
                LONGLONG need = audioTarget - audWritten;
                if (need <= 0) break;
                LONGLONG chunk = min(need, 2000000LL);
                LONGLONG written = WriteSilenceChunk(
                                       pW.Get(), audIdx, audWritten, chunk, pcmBytesNum, pcmHnsDen);
                if (written <= 0) break;
                audWritten += written;
            }
        }
    }

    PostMessage(params->hWnd, WM_ENCODE_PROGRESS, 100, 0);
    pW->Finalize();
    PostMessage(params->hWnd, WM_ENCODE_DONE, 1, 0);
}

// Trampoline so std::thread can hold a unique_ptr
void EncodeThreadEntry(EncodeParams* raw)
{
    EncodeThread(std::unique_ptr<EncodeParams>(raw));
}
