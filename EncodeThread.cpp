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

static void WarnMF(const wchar_t* message, HRESULT hr = S_OK)
{
    wchar_t buffer[256] = {};
    if (FAILED(hr))
        swprintf_s(buffer, L"[Media Foundation warning] %s (0x%08X)\n", message, static_cast<UINT32>(hr));
    else
        swprintf_s(buffer, L"[Media Foundation warning] %s\n", message);
    OutputDebugStringW(buffer);
}

static bool WriteSilenceChunk(
    IMFSinkWriter* pW, DWORD audIdx,
    LONGLONG ts, LONGLONG durationHns,
    LONGLONG bytesPerHns_num, LONGLONG bytesPerHns_den,
    LONGLONG* writtenHns)
{
    *writtenHns = 0;
    if (durationHns <= 0) return true;
    LONGLONG nbytes = (durationHns * bytesPerHns_num + bytesPerHns_den - 1) / bytesPerHns_den;
    nbytes = (nbytes + 3) & ~3LL;
    const LONGLONG MAX_BYTES = 1024 * 1024;
    if (nbytes > MAX_BYTES) {
        nbytes = MAX_BYTES;
        nbytes = (nbytes + 3) & ~3LL;
    }
    LONGLONG realHns = nbytes * bytesPerHns_den / bytesPerHns_num;
    if (realHns <= 0) return false;

    ComPtr<IMFMediaBuffer> pBuf;
    if (FAILED(MFCreateMemoryBuffer((DWORD)nbytes, &pBuf))) return false;
    BYTE* d = nullptr;
    DWORD mL = 0;
    if (FAILED(pBuf->Lock(&d, &mL, nullptr))) return false;
    memset(d, 0, (size_t)nbytes);
    pBuf->Unlock();
    pBuf->SetCurrentLength((DWORD)nbytes);

    ComPtr<IMFSample> pS;
    if (FAILED(MFCreateSample(&pS)) ||
        FAILED(pS->AddBuffer(pBuf.Get())) ||
        FAILED(pS->SetSampleTime(ts)) ||
        FAILED(pS->SetSampleDuration(realHns)) ||
        FAILED(pW->WriteSample(audIdx, pS.Get()))) return false;
    *writtenHns = realHns;
    return true;
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

static HRESULT WriteAudioSample(
    IMFSinkWriter* pW, DWORD audIdx,
    IMFSample* pSrc, LONGLONG ts, float vol, LONGLONG maxDurationHns,
    UINT32 blockAlignment, LONGLONG* writtenHns)
{
    *writtenHns = 0;
    LONGLONG srcDuration = 0;
    // Some source readers omit MF_SAMPLE_DURATION. Keep the same 20 ms
    // fallback used by ProcessAudio, and attach it before handing the sample
    // to the sink writer.
    if (FAILED(pSrc->GetSampleDuration(&srcDuration)) || srcDuration <= 0) {
        srcDuration = 200000LL;
        HRESULT hr = pSrc->SetSampleDuration(srcDuration);
        if (FAILED(hr)) return hr;
    }
    const bool trim = maxDurationHns < srcDuration;
    if (!trim && vol == 1.0f) {
        HRESULT hr = pSrc->SetSampleTime(ts);
        if (FAILED(hr)) return hr;
        hr = pW->WriteSample(audIdx, pSrc);
        if (FAILED(hr)) return hr;
        *writtenHns = srcDuration;
        return S_OK;
    }

    ComPtr<IMFMediaBuffer> pIn;
    HRESULT hr = pSrc->ConvertToContiguousBuffer(&pIn);
    if (FAILED(hr)) return hr;

    BYTE* pInD = nullptr;
    DWORD lenIn = 0;
    hr = pIn->Lock(&pInD, nullptr, &lenIn);
    if (FAILED(hr)) return hr;
    const DWORD lenOut = trim
        ? (DWORD)((((LONGLONG)lenIn * maxDurationHns / srcDuration) / blockAlignment) * blockAlignment)
        : lenIn;
    // A range can end between two PCM frames. There is nothing valid to
    // encode in that fragment; report a normal, empty write to the caller.
    if (lenOut == 0) { pIn->Unlock(); return S_FALSE; }

    ComPtr<IMFMediaBuffer> pOut;
    hr = MFCreateMemoryBuffer(lenOut, &pOut);
    if (FAILED(hr)) { pIn->Unlock(); return hr; }
    BYTE* pOutD = nullptr;
    DWORD mxO = 0;
    hr = pOut->Lock(&pOutD, &mxO, nullptr);
    if (FAILED(hr)) { pIn->Unlock(); return hr; }

    auto* s = (int16_t*)pInD;
    auto* d = (int16_t*)pOutD;
    int n = lenOut / 2;
    for (int i = 0; i < n; i++) {
        float v = s[i] * vol;
        d[i] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
    }

    pOut->Unlock();
    pOut->SetCurrentLength(lenOut);
    pIn->Unlock();

    ComPtr<IMFSample> pDst;
    const LONGLONG outDuration = trim ? (srcDuration * lenOut / lenIn) : srcDuration;
    hr = MFCreateSample(&pDst);
    if (FAILED(hr)) return hr;
    hr = pDst->AddBuffer(pOut.Get());
    if (FAILED(hr)) return hr;
    hr = pDst->SetSampleTime(ts);
    if (FAILED(hr)) return hr;
    hr = pDst->SetSampleDuration(outDuration);
    if (FAILED(hr)) return hr;
    hr = pW->WriteSample(audIdx, pDst.Get());
    if (FAILED(hr)) return hr;
    *writtenHns = outDuration;
    return S_OK;
}


#include "VideoEncoder.h"
#include <optional>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

VideoEncoder::VideoEncoder(std::unique_ptr<EncodeParams> params, std::shared_ptr<std::atomic_bool> cancelRequested)
{
	m_cancelRequested = cancelRequested;
    m_hWnd = params->hWnd;
	m_volumescale = params->volumeScale;
	m_audio_repeat = params->audioShortMode;
    const QualityPreset preset = PRESETS[params->qualityIdx];
    m_pcmBytesNum = (LONGLONG)(44100 * preset.audChannels * 2);

    m_videoStartHns = (LONGLONG)(params->videoStart * 1e7);
    m_videoEndHns = (params->videoEnd > params->videoStart
        && params->videoEnd > 0)
		? (LONGLONG)(params->videoEnd * 1e7) : 0;

    m_audioStartHns = (LONGLONG)(params->audioStart * 1e7);
    m_audioEndHns = (params->audioEnd > params->audioStart
        && params->audioEnd > 0)
        ? (LONGLONG)(params->audioEnd * 1e7) : 0;
    m_audioRangeHns = (m_audioEndHns > 0) ? (m_audioEndHns - m_audioStartHns) : LLONG_MAX;

    m_maxDurHns = (params->videoEnd > params->videoStart && params->videoEnd > 0.0)
        ? (LONGLONG)((params->videoEnd - params->videoStart) * 1e7)
        : LLONG_MAX;
    UINT32 sourceDurHns = MF_GetDuration(params->videoPath);
    if (sourceDurHns > 0) {
        m_outputDurHns = max(0LL, sourceDurHns - (LONGLONG)(params->videoStart * 1e7));
    }
    if (m_maxDurHns != LLONG_MAX)
        m_outputDurHns = min(m_outputDurHns, m_maxDurHns);

    this->Initialize(PRESETS[params->qualityIdx], params->videoPath, params->audioPath, params->outputPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool VideoEncoder::Initialize(QualityPreset preset, std::wstring vfile, std::wstring afile, std::wstring outfile)
{
    m_devMgr = CreateD3DManager();
    if (m_devMgr == nullptr)
        WarnMF(L"Accélération matérielle indisponible ; utilisation du mode logiciel.");
    m_vid = OpenVideoReader(m_devMgr, vfile, preset.maxWidth, preset.maxHeight);
	if (!m_vid) return false;

	m_aud = OpenAudioReader(afile, m_audioStartHns, preset.audChannels);
	if (!m_aud) return false;

    m_writer = CreateSinkWriter(m_devMgr, outfile);
    if (!m_writer) return false;
	
    auto vidIdx = ConfigureVideoStream(*m_vid.get(), m_writer.Get(), preset.maxVidBitrate, preset.h264Profile);
    if (vidIdx == kInvalidStreamIndex) return false;
    m_vidIdx = vidIdx;

    auto audIdx = ConfigureAudioStream(*m_aud.get(), m_writer.Get(), preset.audChannels, preset.audBytesPerSec);
    if (audIdx == kInvalidStreamIndex) return false;
    m_audIdx = audIdx;
    IMFSourceReader* reader = m_vid->reader.Get();
    SeekVideoToStart(reader, m_videoStartHns);
    if (m_failed) return false;
    return true;
}

void VideoEncoder::Run()
{
    if (IsCancellationRequested()) return Cancel();
    if (FAILED(m_writer->BeginWriting()))
        return Fail(L"Erreur démarrage écriture MP4.");

    EncodeLoop loop;
    // Utiliser le lecteur audio préparé dans Initialize() si disponible
    loop.audReader = (m_aud && m_aud->reader) ? m_aud->reader : nullptr;
    QueryPerformanceFrequency(&loop.qpcFreq);
    QueryPerformanceCounter(&loop.qpcStart);

    while (!loop.vidDone)
    {
        if (IsCancellationRequested()) return Cancel();
        ProcessVideoFrame(loop, m_writer.Get(), m_vidIdx);
        if (m_failed) return;
        ProcessAudio(loop, m_writer.Get(), m_audIdx, m_audioRangeHns, m_outputDurHns);
        if (m_failed) return;
    }

    PostMessage(m_hWnd, WM_ENCODE_PROGRESS, 100, 0);
    if (FAILED(m_writer->Finalize()))
        return Fail(L"Impossible de finaliser le fichier MP4.");
    
    ENCODE_DONE_MSG doneMsg = { true, L""};
    SendMessage(m_hWnd, WM_ENCODE_DONE, (WPARAM)&doneMsg, 0);
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
    const ComPtr<IMFDXGIDeviceManager>& devMgr, std::wstring vfile, UINT32 max_out_width, UINT32 max_out_height)
{
    ComPtr<IMFAttributes> pA;
    HRESULT attrHr = MFCreateAttributes(&pA, 3);
    if (FAILED(attrHr)) {
        WarnMF(L"Attributs du lecteur vidéo indisponibles ; configuration par défaut utilisée.", attrHr);
    } else {
        if (FAILED(attrHr = pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)))
            WarnMF(L"Transformations matérielles vidéo non activées.", attrHr);
        if (FAILED(attrHr = pA->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE)))
            WarnMF(L"DXVA vidéo non disponible.", attrHr);
        if (devMgr && FAILED(attrHr = pA->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, devMgr.Get())))
            WarnMF(L"Gestionnaire D3D non attaché au lecteur vidéo.", attrHr);
    }

    auto vid = std::make_unique<VideoSourceInfo>();
    if (FAILED(MFCreateSourceReaderFromURL(vfile.c_str(), pA.Get(), &vid->reader))) {
        Fail(L"Impossible d'ouvrir la vidéo source.");
        return nullptr;
    }

    if (FAILED(vid->reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE)) ||
        FAILED(vid->reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE))) {
        Fail(L"Impossible de sélectionner le flux vidéo source.");
        return nullptr;
    }

    // This is metadata only: quality selection may use it, but its absence
    // must not prevent encoding.
    ComPtr<IMFMediaType> nativeVideoType;
    if (SUCCEEDED(vid->reader->GetNativeMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeVideoType))) {
        nativeVideoType->GetUINT32(MF_MT_AVG_BITRATE, &vid->sourceBitrate);
    }

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

    if (FAILED(vid->reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &vid->actualType)) ||
        !vid->actualType) {
        Fail(L"Erreur lecture type vidéo.");
        return nullptr;
    }

    if (FAILED(MFGetAttributeSize(vid->actualType.Get(), MF_MT_FRAME_SIZE, &vid->width, &vid->height)) ||
        vid->width == 0 || vid->height == 0) {
        Fail(L"Dimensions vidéo source invalides.");
        return nullptr;
    }
    if (FAILED(MFGetAttributeRatio(vid->actualType.Get(), MF_MT_FRAME_RATE, &vid->frNum, &vid->frDen)) ||
        vid->frNum == 0 || vid->frDen == 0) {
        WarnMF(L"Fréquence d'images inconnue ; 30 i/s utilisée.");
        vid->frNum = 30;
        vid->frDen = 1;
    }

    // Compute scaled output dimensions, swapping the bounding box for portrait video
    UINT32 maxW = max_out_width, maxH = max_out_height;
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

std::unique_ptr<AudioSourceInfo> VideoEncoder::OpenAudioReader(std::wstring afile, LONGLONG audioStart, int nb_channels)
{
    ComPtr<IMFSourceReader> pR;
    if (FAILED(MFCreateSourceReaderFromURL(afile.c_str(), nullptr, &pR))) {
        Fail(L"Impossible d'ouvrir le fichier audio.");
        return {};
    }

    if (FAILED(pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE)) ||
        FAILED(pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) {
        Fail(L"Impossible de sélectionner le flux audio source.");
        return {};
    }

    ComPtr<IMFMediaType> pT;
    MFCreateMediaType(&pT);
    pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, nb_channels);
    pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pT->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, nb_channels * 2);
    pT->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                  44100 * nb_channels * 2);
    if (FAILED(pR->SetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT.Get()))) {
        Fail(L"Impossible de décoder l'audio au format PCM demandé.");
        return {};
    }

    if (audioStart > 0.0) { //TODO: move to render loop
        PROPVARIANT v;
        v.vt = VT_I8;
        v.hVal.QuadPart = audioStart;
        if (FAILED(pR->SetCurrentPosition(GUID_NULL, v))) {
            Fail(L"Impossible de positionner l'audio source.");
            return {};
        }
    }
    ComPtr<IMFMediaType> actualType;
    if (FAILED(pR->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actualType))) {
        Fail(L"Impossible de lire le format PCM audio.");
        return nullptr;
    }
    auto aud = std::make_unique<AudioSourceInfo>();

	aud->reader = pR;
	aud->actualType = actualType;
	aud->nbChannels = nb_channels;
	aud->bytesPerSec = 44100 * nb_channels * 2;

    return aud;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 — Sink writer
// ─────────────────────────────────────────────────────────────────────────────

ComPtr<IMFSinkWriter> VideoEncoder::CreateSinkWriter(
    const ComPtr<IMFDXGIDeviceManager>& devMgr, std::wstring outfile)
{
    ComPtr<IMFAttributes> pA;
    HRESULT attrHr = MFCreateAttributes(&pA, 3);
    if (FAILED(attrHr)) {
        WarnMF(L"Attributs du writer indisponibles ; configuration par défaut utilisée.", attrHr);
    } else {
        if (FAILED(attrHr = pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)))
            WarnMF(L"Transformations matérielles du writer non activées.", attrHr);
        if (FAILED(attrHr = pA->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE)))
            WarnMF(L"Throttling du writer non désactivé.", attrHr);
        if (devMgr && FAILED(attrHr = pA->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, devMgr.Get())))
            WarnMF(L"Gestionnaire D3D non attaché au writer.", attrHr);
    }

    ComPtr<IMFSinkWriter> writer;
    if (FAILED(MFCreateSinkWriterFromURL(outfile.c_str(), nullptr, pA.Get(), &writer)))
        return Fail(L"Impossible de créer le fichier de sortie."), nullptr;

    m_outputCreated = true;
    return writer;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 4 — H.264 video output stream
// ─────────────────────────────────────────────────────────────────────────────

DWORD VideoEncoder::ConfigureVideoStream(
    const VideoSourceInfo vid, IMFSinkWriter* writer, UINT32 max_vid_bitrate, UINT32 h264Profile)
{
    UINT32 srcBitrate = vid.sourceBitrate;
    UINT32 vBitrate = (max_vid_bitrate == 0)
        ? ((srcBitrate > 0) ? srcBitrate : 4000000u)
        : ((srcBitrate > 0)
            ? min(max_vid_bitrate, srcBitrate)
            : max_vid_bitrate);

    ComPtr<IMFMediaType> pOut;
    MFCreateMediaType(&pOut);
    pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    pOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(pOut.Get(), MF_MT_FRAME_SIZE, vid.outW, vid.outH);
    MFSetAttributeRatio(pOut.Get(), MF_MT_FRAME_RATE, vid.frNum, vid.frDen);
    MFSetAttributeRatio(pOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    pOut->SetUINT32(MF_MT_AVG_BITRATE, vBitrate);
    pOut->SetUINT32(MF_MT_MPEG2_PROFILE, h264Profile);

    DWORD idx = 0;
    if (FAILED(writer->AddStream(pOut.Get(), &idx))) {
        Fail(L"Erreur ajout flux H264.");
        return kInvalidStreamIndex;
    }

    if (FAILED(writer->SetInputMediaType(idx, vid.actualType.Get(), nullptr))) {
        Fail(L"Type vidéo incompatible avec l'encodeur H264.");
        return kInvalidStreamIndex;
    }

    return idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5 — AAC audio output stream
// ─────────────────────────────────────────────────────────────────────────────

DWORD VideoEncoder::ConfigureAudioStream(const AudioSourceInfo aud, IMFSinkWriter* writer, UINT32 nb_channels, UINT32 bytes_per_sec)
{
    // Output: AAC-LC
    ComPtr<IMFMediaType> pOut;
    MFCreateMediaType(&pOut);
    pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, nb_channels);
    pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes_per_sec);
    pOut->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29); // AAC-LC

    DWORD idx = 0;
    if (FAILED(writer->AddStream(pOut.Get(), &idx))) {
        Fail(L"Erreur ajout flux AAC.");
        return kInvalidStreamIndex;
    }

    // Input: the exact PCM format negotiated with the source reader.
    if (FAILED(writer->SetInputMediaType(idx, aud.actualType.Get(), nullptr))) {
        Fail(L"Erreur configuration AAC. (Windows 7+)");
        return kInvalidStreamIndex;
    }

    return idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6 — Seek video to start
// ─────────────────────────────────────────────────────────────────────────────

void VideoEncoder::SeekVideoToStart(IMFSourceReader* reader, LONGLONG start)
{
    if (start <= 0.0) return;

    PROPVARIANT v;
    v.vt = VT_I8;
    v.hVal.QuadPart = start;
    if (FAILED(reader->SetCurrentPosition(GUID_NULL, v)))
        Fail(L"Impossible de positionner la vidéo source.");
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

    if (FAILED(hr)) {
        Fail(L"Erreur pendant la lecture de la vidéo.");
        return false;
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
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

    if (FAILED(pS->SetSampleTime(rel)) || FAILED(writer->WriteSample(vidIdx, pS.Get()))) {
        Fail(L"Erreur pendant l'écriture de la vidéo.");
        return false;
    }
    loop.vidLastTs = rel;

    if (m_maxDurHns != LLONG_MAX && rel - loop.lastProgressHns > 5000000LL)
        ReportProgress(loop, rel, m_maxDurHns);

    return true;
}

void VideoEncoder::ProcessAudio(EncodeLoop& loop, IMFSinkWriter* writer, DWORD audIdx,
    LONGLONG audioRangeHns, LONGLONG outputDurHns)
{
    const LONGLONG target = min(
        loop.vidLastTs + 2000000LL,
        outputDurHns == LLONG_MAX ? loop.vidLastTs + 2000000LL : outputDurHns);

    while (loop.audWritten < target)
    {
        if (IsCancellationRequested()) return Cancel();
        if (loop.audEOF)
        {
            // Write one chunk of silence and re-evaluate next iteration
            const LONGLONG need = target - loop.audWritten;
            const LONGLONG chunk = min(need, 2000000LL);
            LONGLONG written = 0;
            if (!WriteSilenceChunk(
                writer, audIdx, loop.audWritten,
                chunk, m_pcmBytesNum, kPcmHnsDen, &written)) {
                Fail(L"Erreur pendant l'écriture du silence audio.");
                return;
            }
            if (written <= 0) return;
            loop.audWritten += written;
            continue;
        }

        ComPtr<IMFSample> pS;
        DWORD   flags = 0;
        HRESULT hr = loop.audReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0, nullptr, &flags, nullptr, &pS);

        if (FAILED(hr)) {
            Fail(L"Erreur pendant la lecture de l'audio.");
            return;
        }
        const bool eof = flags & MF_SOURCE_READERF_ENDOFSTREAM;

        if (!pS && !eof) continue;

        if (!eof)
        {
            LONGLONG dur = 0;
            if (FAILED(pS->GetSampleDuration(&dur)) || dur <= 0) dur = 200000LL;

            const LONGLONG rangeRemaining = audioRangeHns == LLONG_MAX
                ? dur : max(0LL, audioRangeHns - loop.audPosInRange);
            const LONGLONG outputRemaining = outputDurHns == LLONG_MAX
                ? dur : max(0LL, outputDurHns - loop.audWritten);
            const LONGLONG writeLimit = min(dur, min(rangeRemaining, outputRemaining));
            if (writeLimit <= 0) {
                loop.audEOF = true;
            } else {
                LONGLONG written = 0;
                const HRESULT writeHr = WriteAudioSample(writer, audIdx, pS.Get(), loop.audWritten,
                    m_volumescale, writeLimit, m_aud->nbChannels * 2, &written);
                if (FAILED(writeHr)) {
                    wchar_t msg[128] = {};
                    swprintf_s(msg, L"Erreur pendant l'écriture de l'audio (0x%08X).",
                               static_cast<UINT32>(writeHr));
                    Fail(msg);
                    return;
                }
                if (writeHr == S_FALSE) {
                    // Do not loop forever on the sub-frame remainder at the
                    // end of the output. For an audio-selection boundary,
                    // mark EOF so the loop/silence policy takes over.
                    if (outputDurHns != LLONG_MAX && outputRemaining < dur) {
                        loop.audWritten = outputDurHns;
                        return;
                    }
                    loop.audEOF = true;
                } else {
                    loop.audWritten += written;
                    loop.audPosInRange += written;
                    if (written < dur) loop.audEOF = true;
                }
            }
        }

        if (eof || loop.audEOF)
        {
            loop.audEOF = true;
            if (this->m_audio_repeat == ASM_LOOP)
            {
                PROPVARIANT position;
                PropVariantInit(&position);
                position.vt = VT_I8;
                position.hVal.QuadPart = (LONGLONG)(m_audioRangeHns * 1e7);
                if (FAILED(loop.audReader->SetCurrentPosition(GUID_NULL, position))) {
                    PropVariantClear(&position);
                    Fail(L"Impossible de relancer l'audio source.");
                    return;
                }
                PropVariantClear(&position);
                {
                    loop.audEOF = false;
                    loop.audPosInRange = 0;
                }
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

    PostMessage(m_hWnd, WM_ENCODE_PROGRESS,
        (WPARAM)pct, (LPARAM)(LONGLONG)eta);
}

void VideoEncoder::Fail(const wchar_t* msg)
{
    if (m_failed) return;
    m_failed = true;
    const wchar_t* raw = (msg && msg[0]) ? msg : L"Erreur inconnue lors de l'encodage.";
    std::wstring errmsg(raw);
    /*
    // Never delete a pre-existing destination when setup failed before the
    // sink writer actually created the new output file.
    if (m_outputCreated && m_params && !m_params->outputPath.empty()) {
        m_writer.Reset();
        DeleteFile(m_params->outputPath.c_str());
    }*/

    if (m_hWnd && IsWindow(m_hWnd)) {
        ENCODE_DONE_MSG doneMsg = { false, errmsg };
        SendMessage(m_hWnd, WM_ENCODE_DONE, (WPARAM)&doneMsg, (LPARAM)0);
    }
}

bool VideoEncoder::IsCancellationRequested() const
{
    return m_cancelRequested && m_cancelRequested->load();
}

void VideoEncoder::Cancel()
{
   /* if (m_outputCreated && m_params && !m_params->outputPath.empty()) {
        m_writer.Reset();
        DeleteFile(m_params->outputPath.c_str());
    }*/
}
