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


#include "VideoEncoder.h"
#include <optional>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

VideoEncoder::VideoEncoder(std::unique_ptr<EncodeParams> params)
    : m_params(std::move(params))
    , m_preset(&PRESETS[m_params->qualityIdx])
{
    m_pcmBytesNum = (LONGLONG)(44100 * m_preset->audChannels * 2);

    const LONGLONG audioStartHns = (LONGLONG)(m_params->audioStart * 1e7);
    const LONGLONG audioEndHns = (m_params->audioEnd > m_params->audioStart
        && m_params->audioEnd > 0)
        ? (LONGLONG)(m_params->audioEnd * 1e7) : 0;
    m_audioRangeHns = (audioEndHns > 0) ? (audioEndHns - audioStartHns) : LLONG_MAX;

    m_maxDurHns = (m_params->videoEnd > m_params->videoStart && m_params->videoEnd > 0.0)
        ? (LONGLONG)((m_params->videoEnd - m_params->videoStart) * 1e7)
        : LLONG_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool VideoEncoder::Initialize()
{
    m_devMgr = CreateD3DManager();          // Step 0 — null is fine, means SW fallback

    m_vid = OpenVideoReader(m_devMgr);   // Step 1
    

    // Step 2 — audio reader is handed straight into EncodeLoop in Run(),
    // but we open it here to catch failures early.
    if (!OpenAudioReaderForJob()) return false;

    m_writer = CreateSinkWriter(m_devMgr);  // Step 3
    if (!m_writer) return false;

    auto vidIdx = ConfigureVideoStream(*m_vid.get(), m_writer.Get()); // Step 4
    if (!vidIdx) return false;
    m_vidIdx = vidIdx;

    auto audIdx = ConfigureAudioStream(m_writer.Get());        // Step 5
    if (!audIdx) return false;
    m_audIdx = audIdx;
    IMFSourceReader* reader = m_vid->reader.Get();
    SeekVideoToStart(reader);   // Step 6
    return true;
}

void VideoEncoder::Run()
{
    if (FAILED(m_writer->BeginWriting()))
        return Fail(L"Erreur démarrage écriture MP4.");

    EncodeLoop loop;
    loop.audReader = OpenAudioReaderForJob();   // re-open (Initialize already validated it)
    QueryPerformanceFrequency(&loop.qpcFreq);
    QueryPerformanceCounter(&loop.qpcStart);

    while (!loop.vidDone)
    {
        ProcessVideoFrame(loop, m_writer.Get(), m_vidIdx);
        ProcessAudio(loop, m_writer.Get(), m_audIdx, m_audioRangeHns, m_maxDurHns);
    }

    PostMessage(m_params->hWnd, WM_ENCODE_PROGRESS, 100, 0);
    m_writer->Finalize();

    PostMessage(m_params->hWnd, WM_ENCODE_DONE, 1, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 0 — D3D11 device + DXGI device manager (non-fatal)
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<IMFDXGIDeviceManager> VideoEncoder::CreateD3DManager()
{
    ComPtr<ID3D11Device>        pDevice;
    ComPtr<ID3D11DeviceContext> pContext;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        &pDevice, nullptr, &pContext);
    if (FAILED(hr)) return nullptr;

    ComPtr<ID3D11Multithread> pMT;
    if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pMT))))
        pMT->SetMultithreadProtected(TRUE);

    UINT resetToken = 0;
    ComPtr<IMFDXGIDeviceManager> devMgr;
    if (FAILED(MFCreateDXGIDeviceManager(&resetToken, &devMgr))) return nullptr;
    if (FAILED(devMgr->ResetDevice(pDevice.Get(), resetToken)))  return nullptr;

    return devMgr; // pDevice/pContext kept alive by devMgr internally
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1 — Video source reader
// ─────────────────────────────────────────────────────────────────────────────

static UINT32 ComputeOutputDimension(UINT32 src, UINT32 max)
{
    // Returns src scaled down to max, then rounded down to even, min 2.
    UINT32 out = (max > 0 && src > max) ? max : src;
    return max(2u, out & ~1u);
}

std::unique_ptr<VideoSourceInfo> VideoEncoder::OpenVideoReader(
    const ComPtr<IMFDXGIDeviceManager>& devMgr)
{
    ComPtr<IMFAttributes> pA;
    MFCreateAttributes(&pA, 3);
    pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    pA->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
    if (devMgr)
        pA->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, devMgr.Get());

    auto vid = std::make_unique<VideoSourceInfo>();
    if (FAILED(MFCreateSourceReaderFromURL(m_params->videoPath.c_str(), pA.Get(), &vid->reader))) {
        Fail(L"Impossible d'ouvrir la vidéo source.");
        return nullptr;
    }

    vid->reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    vid->reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    // Prefer GPU-friendly formats: NV12 > P010 > YUY2
    {
        ComPtr<IMFMediaType> pDecT;
        MFCreateMediaType(&pDecT);
        pDecT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

        const GUID subtypes[] = { MFVideoFormat_NV12, MFVideoFormat_P010, MFVideoFormat_YUY2 };
        HRESULT hr = E_FAIL;
        for (const GUID& st : subtypes) {
            pDecT->SetGUID(MF_MT_SUBTYPE, st);
            hr = vid->reader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pDecT.Get());
            if (SUCCEEDED(hr)) break;
        }
        if (FAILED(hr)) {
            Fail(L"Impossible de décoder la vidéo.");
            return nullptr;
        }
    }

    vid->reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &vid->actualType);
    if (!vid->actualType) {
        Fail(L"Erreur lecture type vidéo.");
        return nullptr;
    }

    MFGetAttributeSize(vid->actualType.Get(), MF_MT_FRAME_SIZE, &vid->width, &vid->height);
    MFGetAttributeRatio(vid->actualType.Get(), MF_MT_FRAME_RATE, &vid->frNum, &vid->frDen);
    if (vid->frNum == 0) { vid->frNum = 30; vid->frDen = 1; }

    // Compute scaled output dimensions, swapping the bounding box for portrait video
    UINT32 maxW = m_preset->maxWidth, maxH = m_preset->maxHeight;
    if (vid->height > vid->width && maxW > 0 && maxH > 0) std::swap(maxW, maxH);

    float scaleW = (maxW > 0 && vid->width > maxW) ? (float)maxW / vid->width : 1.0f;
    float scaleH = (maxH > 0 && vid->height > maxH) ? (float)maxH / vid->height : 1.0f;
    float scale = min(scaleW, scaleH);

    vid->outW = ComputeOutputDimension((UINT32)(vid->width * scale), 0);
    vid->outH = ComputeOutputDimension((UINT32)(vid->height * scale), 0);

    return vid;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 2 — Audio source reader
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<IMFSourceReader> VideoEncoder::OpenAudioReaderForJob()
{
    ComPtr<IMFSourceReader> pR;
    if (FAILED(MFCreateSourceReaderFromURL(m_params->audioPath.c_str(), nullptr, &pR))) return {};

    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> pT;
    MFCreateMediaType(&pT);
    pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)m_preset->audChannels);
    pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (FAILED(pR->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT.Get())))
        return {};

    if (m_params->audioStart > 0.0) {
        PROPVARIANT v;
        v.vt = VT_I8;
        v.hVal.QuadPart = (LONGLONG)(m_params->audioStart * 1e7);
        pR->SetCurrentPosition(GUID_NULL, v);
    }
    if (!pR) {
        Fail(L"Impossible de d\u00e9coder l'audio.");
        return nullptr;
    }
    return pR;

}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 — Sink writer
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<IMFSinkWriter> VideoEncoder::CreateSinkWriter(
    const ComPtr<IMFDXGIDeviceManager>& devMgr)
{
    ComPtr<IMFAttributes> pA;
    MFCreateAttributes(&pA, 3);
    pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    pA->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (devMgr)
        pA->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, devMgr.Get());

    ComPtr<IMFSinkWriter> writer;
    if (FAILED(MFCreateSinkWriterFromURL(m_params->outputPath.c_str(), nullptr, pA.Get(), &writer)))
        return Fail(L"Impossible de créer le fichier de sortie."), nullptr;

    return writer;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 4 — H.264 video output stream
// ─────────────────────────────────────────────────────────────────────────────

DWORD VideoEncoder::ConfigureVideoStream(
    const VideoSourceInfo vid, IMFSinkWriter* writer)
{
    UINT32 srcBitrate = MF_GetVideoBitrate(m_params->videoPath);
    UINT32 vBitrate = (m_preset->maxVidBitrate == 0)
        ? ((srcBitrate > 0) ? srcBitrate : 4000000u)
        : ((srcBitrate > 0)
            ? min(m_preset->maxVidBitrate, srcBitrate)
            : m_preset->maxVidBitrate);

    ComPtr<IMFMediaType> pOut;
    MFCreateMediaType(&pOut);
    pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    pOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(pOut.Get(), MF_MT_FRAME_SIZE, vid.outW, vid.outH);
    MFSetAttributeRatio(pOut.Get(), MF_MT_FRAME_RATE, vid.frNum, vid.frDen);
    MFSetAttributeRatio(pOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    pOut->SetUINT32(MF_MT_AVG_BITRATE, vBitrate);
    pOut->SetUINT32(MF_MT_MPEG2_PROFILE, (UINT32)m_preset->h264Profile);

    DWORD idx = 0;
    if (FAILED(writer->AddStream(pOut.Get(), &idx))) {
        Fail(L"Erreur ajout flux H264.");
        return -1;
    }

    if (FAILED(writer->SetInputMediaType(idx, vid.actualType.Get(), nullptr))) {
        Fail(L"Type vidéo incompatible avec l'encodeur H264.");
        return -1;
    }

    return idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5 — AAC audio output stream
// ─────────────────────────────────────────────────────────────────────────────

DWORD VideoEncoder::ConfigureAudioStream(IMFSinkWriter* writer)
{
    // Output: AAC-LC
    ComPtr<IMFMediaType> pOut;
    MFCreateMediaType(&pOut);
    pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)m_preset->audChannels);
    pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_preset->audBytesPerSec);
    pOut->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29); // AAC-LC

    DWORD idx = 0;
    if (FAILED(writer->AddStream(pOut.Get(), &idx))) {
        Fail(L"Erreur ajout flux AAC.");
        return -1;
    }

    // Input: raw PCM (what OpenAudioReader produces)
    ComPtr<IMFMediaType> pIn;
    MFCreateMediaType(&pIn);
    pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)m_preset->audChannels);

    if (FAILED(writer->SetInputMediaType(idx, pIn.Get(), nullptr))) {
        Fail(L"Erreur configuration AAC. (Windows 7+)");
        return -1;
    }

    return idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6 — Seek video to start
// ─────────────────────────────────────────────────────────────────────────────

void VideoEncoder::SeekVideoToStart(IMFSourceReader* reader)
{
    if (m_params->videoStart <= 0.0) return;

    PROPVARIANT v;
    v.vt = VT_I8;
    v.hVal.QuadPart = (LONGLONG)(m_params->videoStart * 1e7);
    reader->SetCurrentPosition(GUID_NULL, v);
}

// ─────────────────────────────────────────────────────────────────────────────
// Encode-loop helpers
// ─────────────────────────────────────────────────────────────────────────────

bool VideoEncoder::ProcessVideoFrame(EncodeLoop& loop, IMFSinkWriter* writer, DWORD vidIdx)
{
    ComPtr<IMFSample> pS;
    DWORD    flags = 0;
    LONGLONG ts = 0;

    HRESULT hr = m_vid->reader->ReadSample(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0, nullptr, &flags, &ts, &pS);

    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
        return loop.vidDone = true, false;

    if (!pS)
    {
        if (++loop.nullStreak > 200) loop.vidDone = true;
        return false;
    }

    loop.nullStreak = 0;
    if (loop.vidTimeBase < 0) loop.vidTimeBase = ts;

    const LONGLONG rel = ts - loop.vidTimeBase;

    if (m_maxDurHns != LLONG_MAX && rel >= m_maxDurHns)
        return loop.vidDone = true, false;

    pS->SetSampleTime(rel);
    writer->WriteSample(vidIdx, pS.Get());
    loop.vidLastTs = rel;

    if (m_maxDurHns != LLONG_MAX && rel - loop.lastProgressHns > 5000000LL)
        ReportProgress(loop, rel, m_maxDurHns);

    return true;
}

void VideoEncoder::ProcessAudio(EncodeLoop& loop, IMFSinkWriter* writer, DWORD audIdx,
    LONGLONG audioRangeHns, LONGLONG maxDurHns)
{
    const LONGLONG target = min(
        loop.vidLastTs + 2000000LL,
        maxDurHns == LLONG_MAX ? loop.vidLastTs + 2000000LL : maxDurHns);

    while (loop.audWritten < target)
    {
        if (loop.audEOF)
        {
            // Write one chunk of silence and re-evaluate next iteration
            const LONGLONG need = target - loop.audWritten;
            const LONGLONG chunk = min(need, 2000000LL);
            const LONGLONG written = WriteSilenceChunk(
                writer, audIdx, loop.audWritten,
                chunk, m_pcmBytesNum, kPcmHnsDen);
            if (written <= 0) break;
            loop.audWritten += written;
            continue;
        }

        ComPtr<IMFSample> pS;
        DWORD   flags = 0;
        HRESULT hr = loop.audReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0, nullptr, &flags, nullptr, &pS);

        const bool eof = FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) || !pS;

        if (!eof)
        {
            LONGLONG dur = 0;
            if (FAILED(pS->GetSampleDuration(&dur)) || dur <= 0) dur = 200000LL;

            if (audioRangeHns != LLONG_MAX && loop.audPosInRange + dur > audioRangeHns)
            {
                loop.audEOF = true;
            }
            else
            {
                WriteAudioSample(writer, audIdx, pS.Get(), loop.audWritten,
                    m_params->volumeScale);
                loop.audWritten += dur;
                loop.audPosInRange += dur;
            }
        }

        if (eof || loop.audEOF)
        {
            loop.audEOF = true;
            if (m_params->audioShortMode == ASM_LOOP)
            {
                loop.audReader = OpenAudioReaderForJob();
                if (loop.audReader) { loop.audEOF = false; loop.audPosInRange = 0; }
            }
        }
    }
}

void VideoEncoder::ReportProgress(EncodeLoop& loop, LONGLONG relHns, LONGLONG maxDurHns)
{
    loop.lastProgressHns = relHns;

    const int pct = (int)min(99LL, relHns * 100 / maxDurHns);

    LARGE_INTEGER qpcNow;
    QueryPerformanceCounter(&qpcNow);
    const double elapsed = (double)(qpcNow.QuadPart - loop.qpcStart.QuadPart)
        / loop.qpcFreq.QuadPart;
    const double ratio = (elapsed > 0.0 && relHns > 0)
        ? (double)relHns / (elapsed * 1e7) : 1.0;
    const double eta = ((maxDurHns - relHns) / 1e7) / ratio;

    PostMessage(m_params->hWnd, WM_ENCODE_PROGRESS,
        (WPARAM)pct, (LPARAM)(LONGLONG)eta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error handling
// ─────────────────────────────────────────────────────────────────────────────

void VideoEncoder::Fail(const wchar_t* msg)
{
    DeleteFile(m_params->outputPath.c_str());
    PostMessage(m_params->hWnd, WM_ENCODE_DONE, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Free function — replaces the original EncodeThread
// ─────────────────────────────────────────────────────────────────────────────

static void EncodeThread(std::unique_ptr<EncodeParams> params)
{
    VideoEncoder encoder(std::move(params));
    if (!encoder.Initialize()) return;
    encoder.Run();
}