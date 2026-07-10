// ===========================================================================
//  Video Music Replacer / Remplaceur de Musique Video  v6
//  Win32 + Media Foundation -- MSVC 2019/2022 -- C++17 x64
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <codecapi.h>
#include "resource.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <cstring>

#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mfreadwrite.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"propsys.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")

// ---------------------------------------------------------------------------
// IDs controles
// ---------------------------------------------------------------------------
#define ID_BTN_VIDEO        101
#define ID_EDIT_VIDEO       102
#define ID_BTN_AUDIO        103
#define ID_EDIT_AUDIO       104
#define ID_EDIT_VID_START   105
#define ID_EDIT_VID_END     106
#define ID_EDIT_AUD_START   107
#define ID_EDIT_AUD_END     117
#define ID_BTN_GO           108
#define ID_STATIC_STATUS    109
#define ID_PROGRESS         110
#define ID_STATIC_VID_DUR   112
#define ID_STATIC_AUD_DUR   113
#define ID_COMBO_QUALITY    114
#define ID_COMBO_AUDSHORT   115
#define ID_STATIC_QINFO     116
#define ID_SLIDER_VOLUME    118
#define ID_STATIC_VOL       119
#define WM_ENCODE_DONE      (WM_USER+1)
#define WM_WAVEFORM_READY   (WM_USER+2)
#define WM_ENCODE_PROGRESS  (WM_USER+3)   // wParam = 0..100

// ---------------------------------------------------------------------------
// Couleurs / constantes
// ---------------------------------------------------------------------------
static const COLORREF CLR_BG = RGB(244, 244, 248);
static const COLORREF CLR_WAVE_BG = RGB(26, 26, 46);
static const COLORREF CLR_CUR_START = RGB(255, 107, 107);
static const COLORREF CLR_CUR_END = RGB(80, 220, 120);
static const COLORREF CLR_ZONE = RGB(42, 42, 78);
static const COLORREF CLR_ZONE_SEL = RGB(60, 100, 60);
static const COLORREF CLR_TEXT = RGB(30, 30, 40);

static const int WAVEFORM_H = 90;
static const int WAVE_SAMPLES = 700;
static const int MARGIN = 16;
static const int ROW_H = 26;
static const int WINW = 680;

static const LONGLONG PCM_BYTES_NUM = 176400LL;
static const LONGLONG PCM_HNS_DEN = 10000000LL;

// ---------------------------------------------------------------------------
// Localisation — chargement depuis ressources
// ---------------------------------------------------------------------------
static HINSTANCE g_hInst = nullptr;
static LANGID    g_langId = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);

// Charger une chaine depuis la ressource dans la bonne langue
static std::wstring LoadStr(UINT id)
{
    // Essayer la langue preferee, sinon langue neutre
    wchar_t buf[512] = {};
    if (LoadStringW(g_hInst, id, buf, 512) > 0) return buf;
    return L"";
}
// Alias court
static std::wstring S(UINT id) {
    return LoadStr(id);
}

// Formater avec un entier (%d %%)
static std::wstring Sfmt(UINT id, int v)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, LoadStr(id).c_str(), v);
    return buf;
}
// Formater avec deux strings (%s %s)
static std::wstring Sfmt2(UINT id, const wchar_t* a, const wchar_t* b)
{
    wchar_t buf[512] = {};
    swprintf_s(buf, LoadStr(id).c_str(), a, b);
    return buf;
}

// ---------------------------------------------------------------------------
// Presets qualite (labels et descs via IDs ressource)
// ---------------------------------------------------------------------------
struct QualityPreset {
    UINT   lblId, dscId;
    UINT32 maxVidBitrate;   // 0 = source
    UINT32 audBytesPerSec;
    int    audChannels;     // 1=mono (mail/MMS), 2=stereo
    UINT32 maxWidth;
    UINT32 maxHeight;
    int    h264Profile;
};
static const QualityPreset PRESETS[] = {
    { IDS_PRE0_LBL, IDS_PRE0_DSC,       0, 24000, 2,    0,    0, eAVEncH264VProfile_High     },
    { IDS_PRE1_LBL, IDS_PRE1_DSC, 5000000, 16000, 2, 1920, 1080, eAVEncH264VProfile_High     },
    { IDS_PRE2_LBL, IDS_PRE2_DSC, 2000000, 16000, 2, 1280,  720, eAVEncH264VProfile_Main     },
    { IDS_PRE3_LBL, IDS_PRE3_DSC,  400000, 12000, 1,  854,  480, eAVEncH264VProfile_Main     },
    { IDS_PRE4_LBL, IDS_PRE4_DSC,  150000, 12000, 1,  426,  240, eAVEncH264VProfile_Base     },
};
static const int N_PRESETS = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
static int            g_qualityIdx = 1;
enum  AudioShortMode { ASM_LOOP = 0, ASM_SILENCE = 1 };
static AudioShortMode g_audioShortMode = ASM_LOOP;
static int            g_volumePct = 100;

// ---------------------------------------------------------------------------
// Etat global
// ---------------------------------------------------------------------------
struct AppState {
    HWND   hWnd = nullptr, hWaveWnd = nullptr;
    HFONT  hFontUI = nullptr, hFontBold = nullptr, hFontSm = nullptr;
    HBRUSH hBrushBg = nullptr;
    ITaskbarList3* pTaskbar = nullptr;   // ITaskbarList3 pour progression taskbar

    std::wstring videoPath, audioPath;
    double videoDuration = 0, audioDuration = 0;
    double audioStartSec = 0, audioEndSec = 0;
    bool   draggingEnd = false;

    std::vector<float> waveform;
    bool   waveformReady = false;
    std::atomic<bool> encoding{ false };
    std::wstring lastError;
};
static AppState g;

// ---------------------------------------------------------------------------
// Helpers ITaskbarList3
// ---------------------------------------------------------------------------
static void TBProgress(int pct)
{
    if (!g.pTaskbar || !g.hWnd) return;
    g.pTaskbar->SetProgressState(g.hWnd, TBPF_NORMAL);
    g.pTaskbar->SetProgressValue(g.hWnd, (ULONGLONG)pct, 100ULL);
}
static void TBDone(bool ok)
{
    if (!g.pTaskbar || !g.hWnd) return;
    g.pTaskbar->SetProgressState(g.hWnd, ok ? TBPF_NOPROGRESS : TBPF_ERROR);
}

// ---------------------------------------------------------------------------
// Utilitaires
// ---------------------------------------------------------------------------
static std::wstring SecsToHMS(double s)
{
    if (s < 0)s = 0;
    int t = (int)s, h = t / 3600, m = (t % 3600) / 60, sc = t % 60;
    wchar_t b[32];
    swprintf_s(b, L"%02d:%02d:%02d", h, m, sc);
    return b;
}
static double HMSToSecs(const std::wstring& t)
{
    int h = 0, m = 0;
    double s = 0;
    if (swscanf_s(t.c_str(), L"%d:%d:%lf", &h, &m, &s) >= 2) return h * 3600.0 + m * 60.0 + s;
    if (swscanf_s(t.c_str(), L"%lf", &s) == 1) return s;
    return -1.0;
}
static std::wstring CtrlText(HWND p, int id)
{
    HWND h = GetDlgItem(p, id);
    int n = GetWindowTextLength(h) + 1;
    std::wstring s(n, L'\0');
    GetWindowText(h, s.data(), n);
    s.resize(wcslen(s.c_str()));
    return s;
}
static void ErrBox(HWND h, UINT msgId, UINT titleId = IDS_ERR_TITLE_VAL)
{
    MessageBox(h, S(msgId).c_str(), S(titleId).c_str(), MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// MF helpers
// ---------------------------------------------------------------------------
static double MF_GetDuration(const std::wstring& path)
{
    IMFSourceReader* r = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &r))) return 0.0;
    PROPVARIANT v;
    PropVariantInit(&v);
    double d = 0;
    if (SUCCEEDED(r->GetPresentationAttribute(
                      (DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8)
        d = (double)v.uhVal.QuadPart / 1e7;
    PropVariantClear(&v);
    r->Release();
    return d;
}
static UINT32 MF_GetVideoBitrate(const std::wstring& path)
{
    IMFSourceReader* r = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &r))) return 0;
    IMFMediaType* pT = nullptr;
    UINT32 br = 0;
    if (SUCCEEDED(r->GetNativeMediaType(
                      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pT))) {
        pT->GetUINT32(MF_MT_AVG_BITRATE, &br);
        pT->Release();
    }
    r->Release();
    return br;
}

// ---------------------------------------------------------------------------
// Waveform extraction (thread arriere-plan)
// ---------------------------------------------------------------------------
static void ExtractWaveformThread(std::wstring path, HWND hWnd)
{
    IMFSourceReader* pR = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &pR)))
    {
        PostMessage(hWnd, WM_WAVEFORM_READY, 0, 0);
        return;
    }
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    IMFMediaType* pT = nullptr;
    MFCreateMediaType(&pT);
    pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
    pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 8000);
    pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pR->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT);
    pT->Release();
    std::vector<int16_t> pcm;
    pcm.reserve(8000 * 300);
    while (true) {
        IMFSample* pS = nullptr;
        DWORD flags = 0;
        HRESULT hr = pR->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                    0, nullptr, &flags, nullptr, &pS);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (pS)pS->Release();
            break;
        }
        if (!pS) continue;
        IMFMediaBuffer* pB = nullptr;
        if (SUCCEEDED(pS->ConvertToContiguousBuffer(&pB))) {
            BYTE* d = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(pB->Lock(&d, nullptr, &len))) {
                int n = len / 2;
                auto* s16 = reinterpret_cast<int16_t*>(d);
                for (int i = 0; i < n; i++) pcm.push_back(s16[i]);
                pB->Unlock();
            }
            pB->Release();
        }
        pS->Release();
    }
    pR->Release();
    auto* wf = new std::vector<float>(WAVE_SAMPLES, 0.f);
    if (!pcm.empty()) {
        size_t chunk = std::max<size_t>(1, pcm.size() / WAVE_SAMPLES);
        for (int i = 0; i < WAVE_SAMPLES; i++) {
            size_t fr = i * chunk, to = std::min(fr + chunk, pcm.size());
            float pk = 0;
            for (size_t j = fr; j < to; j++) pk = std::max(pk, std::abs((float)pcm[j]));
            (*wf)[i] = std::min(1.f, pk / 32768.f);
        }
    }
    PostMessage(hWnd, WM_WAVEFORM_READY, 0, (LPARAM)wf);
}

// ---------------------------------------------------------------------------
// Encodage
// ---------------------------------------------------------------------------
struct EncodeParams {
    std::wstring videoPath, audioPath, outputPath;
    double videoStart, videoEnd;
    double audioStart, audioEnd;
    AudioShortMode audioShortMode;
    int    qualityIdx;
    float  volumeScale;
    HWND   hWnd;
};

static IMFSourceReader* OpenAudioReader(const std::wstring& path, double seekSecs, int channels)
{
    IMFSourceReader* pR = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &pR))) return nullptr;
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pR->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    IMFMediaType* pT = nullptr;
    MFCreateMediaType(&pT);
    pT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
    pT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (FAILED(pR->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pT)))
    {
        pT->Release();
        pR->Release();
        return nullptr;
    }
    pT->Release();
    if (seekSecs > 0.0) {
        PROPVARIANT v;
        v.vt = VT_I8;
        v.hVal.QuadPart = (LONGLONG)(seekSecs * 1e7);
        pR->SetCurrentPosition(GUID_NULL, v);
    }
    return pR;
}

static void WriteAudioSample(IMFSinkWriter* pW, DWORD audIdx,
                             IMFSample* pSrc, LONGLONG ts, float vol)
{
    if (vol == 1.0f) {
        pSrc->SetSampleTime(ts);
        pW->WriteSample(audIdx, pSrc);
        return;
    }
    IMFMediaBuffer* pIn = nullptr;
    if (FAILED(pSrc->ConvertToContiguousBuffer(&pIn))) return;
    BYTE* pInD = nullptr;
    DWORD lenIn = 0;
    pIn->Lock(&pInD, nullptr, &lenIn);
    IMFMediaBuffer* pOut = nullptr;
    MFCreateMemoryBuffer(lenIn, &pOut);
    BYTE* pOutD = nullptr;
    DWORD mxO = 0;
    pOut->Lock(&pOutD, &mxO, nullptr);
    int16_t* s = (int16_t*)pInD;
    int16_t* d = (int16_t*)pOutD;
    int n = lenIn / 2;
    for (int i = 0; i < n; i++) {
        float v = s[i] * vol;
        d[i] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
    }
    pOut->Unlock();
    pOut->SetCurrentLength(lenIn);
    pIn->Unlock();
    pIn->Release();
    IMFSample* pDst = nullptr;
    MFCreateSample(&pDst);
    pDst->AddBuffer(pOut);
    pOut->Release();
    LONGLONG dur = 0;
    pSrc->GetSampleDuration(&dur);
    pDst->SetSampleTime(ts);
    pDst->SetSampleDuration(dur);
    pW->WriteSample(audIdx, pDst);
    pDst->Release();
}

static LONGLONG WriteSilenceChunk(IMFSinkWriter* pW, DWORD audIdx,
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
    IMFMediaBuffer* pBuf = nullptr;
    if (FAILED(MFCreateMemoryBuffer((DWORD)nbytes, &pBuf))) return 0;
    BYTE* d = nullptr;
    DWORD mL = 0;
    pBuf->Lock(&d, &mL, nullptr);
    memset(d, 0, (size_t)nbytes);
    pBuf->Unlock();
    pBuf->SetCurrentLength((DWORD)nbytes);
    IMFSample* pS = nullptr;
    MFCreateSample(&pS);
    pS->AddBuffer(pBuf);
    pBuf->Release();
    pS->SetSampleTime(ts);
    pS->SetSampleDuration(realHns);
    pW->WriteSample(audIdx, pS);
    pS->Release();
    return realHns;
}

static void EncodeThread(EncodeParams* raw)
{
    std::unique_ptr<EncodeParams> params(raw);
    IMFSourceReader* pVid = nullptr;
    IMFSourceReader* pAud = nullptr;
    IMFSinkWriter* pW = nullptr;
    DWORD vidIdx = 0, audIdx = 1;

    const QualityPreset& preset = PRESETS[params->qualityIdx];
    // bytes PCM par hns selon le nombre de canaux
    const LONGLONG pcmBytesNum = (LONGLONG)(44100 * preset.audChannels * 2); // bytes/s
    const LONGLONG pcmHnsDen = 10000000LL;

    auto Fail = [&](UINT msgId) {
        g.lastError = S(msgId);
        if (pVid) {
            pVid->Release();
            pVid = nullptr;
        }
        if (pAud) {
            pAud->Release();
            pAud = nullptr;
        }
        if (pW) {
            pW->Finalize();
            pW->Release();
            pW = nullptr;
        }
        DeleteFile(params->outputPath.c_str());
        PostMessage(params->hWnd, WM_ENCODE_DONE, 0, 0);
    };
    // Fallback si la string resource n'existe pas encore
    auto FailW = [&](const wchar_t* msg) {
        g.lastError = msg;
        if (pVid) {
            pVid->Release();
            pVid = nullptr;
        }
        if (pAud) {
            pAud->Release();
            pAud = nullptr;
        }
        if (pW) {
            pW->Finalize();
            pW->Release();
            pW = nullptr;
        }
        DeleteFile(params->outputPath.c_str());
        PostMessage(params->hWnd, WM_ENCODE_DONE, 0, 0);
    };

    LONGLONG audioStartHns = (LONGLONG)(params->audioStart * 1e7);
    LONGLONG audioEndHns = (params->audioEnd > params->audioStart && params->audioEnd > 0)
                           ? (LONGLONG)(params->audioEnd * 1e7) : 0;
    LONGLONG audioRangeHns = (audioEndHns > 0) ? (audioEndHns - audioStartHns) : LLONG_MAX;

    // ── 1. Video ─────────────────────────────────────────────────────────────
    {
        IMFAttributes* pA = nullptr;
        MFCreateAttributes(&pA, 2);
        pA->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        HRESULT hr = MFCreateSourceReaderFromURL(params->videoPath.c_str(), pA, &pVid);
        pA->Release();
        if (FAILED(hr)) return FailW(L"Impossible d'ouvrir la vid\u00e9o source.");
    }
    pVid->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pVid->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    IMFMediaType* pDecT = nullptr;
    MFCreateMediaType(&pDecT);
    pDecT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pDecT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
    HRESULT hr = pVid->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pDecT);
    if (FAILED(hr)) {
        pDecT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        hr = pVid->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pDecT);
    }
    pDecT->Release();
    if (FAILED(hr)) return FailW(L"Impossible de d\u00e9coder la vid\u00e9o.");

    IMFMediaType* pVidActual = nullptr;
    pVid->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pVidActual);
    if (!pVidActual) return FailW(L"Erreur lecture type vid\u00e9o.");

    UINT32 vidW = 0, vidH = 0;
    MFGetAttributeSize(pVidActual, MF_MT_FRAME_SIZE, &vidW, &vidH);
    UINT32 frNum = 0, frDen = 1;
    MFGetAttributeRatio(pVidActual, MF_MT_FRAME_RATE, &frNum, &frDen);
    if (frNum == 0) {
        frNum = 30;
        frDen = 1;
    }

    // ── 2. Audio ──────────────────────────────────────────────────────────────
    pAud = OpenAudioReader(params->audioPath, params->audioStart, preset.audChannels);
    if (!pAud) {
        pVidActual->Release();
        return FailW(L"Impossible de d\u00e9coder l'audio.");
    }
    LONGLONG audPosInRange = 0;

    // ── 3. SinkWriter ────────────────────────────────────────────────────────
    {
        IMFAttributes* pA = nullptr;
        MFCreateAttributes(&pA, 2);
        pA->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        pA->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        hr = MFCreateSinkWriterFromURL(params->outputPath.c_str(), nullptr, pA, &pW);
        pA->Release();
    }
    if (FAILED(hr)) {
        pVidActual->Release();
        return FailW(L"Impossible de cr\u00e9er le fichier de sortie.");
    }

    // ── 4. Flux video (H.264) ────────────────────────────────────────────────
    UINT32 outW = vidW, outH = vidH;
    if (preset.maxWidth > 0 && outW > preset.maxWidth) {
        float r = (float)preset.maxWidth / outW;
        outW = preset.maxWidth;
        outH = ((UINT32)(outH * r)) & ~1u;
    }
    if (preset.maxHeight > 0 && outH > preset.maxHeight) {
        float r = (float)preset.maxHeight / outH;
        outH = preset.maxHeight;
        outW = ((UINT32)(outW * r)) & ~1u;
    }
    outW = (outW + 1) & ~1u;
    outH = (outH + 1) & ~1u;

    UINT32 srcBitrate = MF_GetVideoBitrate(params->videoPath);
    UINT32 vBitrate = (preset.maxVidBitrate == 0)
                      ? ((srcBitrate > 0) ? srcBitrate : 4000000)
                      : ((srcBitrate > 0) ? std::min(preset.maxVidBitrate, srcBitrate) : preset.maxVidBitrate);

    IMFMediaType* pVidOut = nullptr;
    MFCreateMediaType(&pVidOut);
    pVidOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pVidOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    pVidOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(pVidOut, MF_MT_FRAME_SIZE, outW, outH);
    MFSetAttributeRatio(pVidOut, MF_MT_FRAME_RATE, frNum, frDen);
    MFSetAttributeRatio(pVidOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    pVidOut->SetUINT32(MF_MT_AVG_BITRATE, vBitrate);
    pVidOut->SetUINT32(MF_MT_MPEG2_PROFILE, (UINT32)preset.h264Profile);
    hr = pW->AddStream(pVidOut, &vidIdx);
    pVidOut->Release();
    if (FAILED(hr)) {
        pVidActual->Release();
        return FailW(L"Erreur ajout flux H264.");
    }
    hr = pW->SetInputMediaType(vidIdx, pVidActual, nullptr);
    pVidActual->Release();
    if (FAILED(hr)) return FailW(L"Type vid\u00e9o incompatible avec l'encodeur H264.");

    // ── 5. Flux audio (AAC-LC) ───────────────────────────────────────────────
    IMFMediaType* pAudOut = nullptr;
    MFCreateMediaType(&pAudOut);
    pAudOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pAudOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    pAudOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pAudOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pAudOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)preset.audChannels);
    pAudOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, preset.audBytesPerSec);
    pAudOut->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29); // AAC-LC
    hr = pW->AddStream(pAudOut, &audIdx);
    pAudOut->Release();
    if (FAILED(hr)) return FailW(L"Erreur ajout flux AAC.");

    IMFMediaType* pAudIn = nullptr;
    MFCreateMediaType(&pAudIn);
    pAudIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pAudIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pAudIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pAudIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    pAudIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)preset.audChannels);
    hr = pW->SetInputMediaType(audIdx, pAudIn, nullptr);
    pAudIn->Release();
    if (FAILED(hr)) return FailW(L"Erreur configuration AAC. (Windows 7+)");

    // ── 6. Seek video ────────────────────────────────────────────────────────
    if (params->videoStart > 0.0) {
        PROPVARIANT v;
        v.vt = VT_I8;
        v.hVal.QuadPart = (LONGLONG)(params->videoStart * 1e7);
        pVid->SetCurrentPosition(GUID_NULL, v);
    }

    // ── 7. Boucle d'encodage ─────────────────────────────────────────────────
    hr = pW->BeginWriting();
    if (FAILED(hr)) return FailW(L"Erreur d\u00e9marrage \u00e9criture MP4.");

    LONGLONG maxDurHns = (params->videoEnd > params->videoStart && params->videoEnd > 0.0)
                         ? (LONGLONG)((params->videoEnd - params->videoStart) * 1e7) : LLONG_MAX;

    LONGLONG vidTimeBase = -1, vidLastTs = 0, audWritten = 0;
    bool vidDone = false, audEOF = false;
    LONGLONG lastProgressHns = 0;
    int nullStreak = 0;

    while (!vidDone) {
        // Frame video
        {
            IMFSample* pS = nullptr;
            DWORD flags = 0;
            LONGLONG ts = 0;
            hr = pVid->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  0, nullptr, &flags, &ts, &pS);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                vidDone = true;
                if (pS) {
                    pS->Release();
                }
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
                    pW->WriteSample(vidIdx, pS);
                    vidLastTs = rel;
                    if (maxDurHns != LLONG_MAX && rel - lastProgressHns > 5000000LL) {
                        int pct = (int)std::min(99LL, rel * 100 / maxDurHns);
                        PostMessage(params->hWnd, WM_ENCODE_PROGRESS, (WPARAM)pct, 0);
                        lastProgressHns = rel;
                    }
                }
                pS->Release();
            }
            else {
                if (++nullStreak > 200) vidDone = true;
            }
        }

        LONGLONG audioTarget = std::min(vidLastTs + 2000000LL,
                                        maxDurHns == LLONG_MAX ? vidLastTs + 2000000LL : maxDurHns);

        while (audWritten < audioTarget) {
            if (!audEOF) {
                IMFSample* pS = nullptr;
                DWORD flags = 0;
                hr = pAud->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                      0, nullptr, &flags, nullptr, &pS);
                bool eof = (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM));
                if (pS && !eof) {
                    LONGLONG dur = 0;
                    if (FAILED(pS->GetSampleDuration(&dur)) || dur <= 0) dur = 200000;
                    if (audioRangeHns != LLONG_MAX && audPosInRange + dur > audioRangeHns) {
                        pS->Release();
                        eof = true;
                    }
                    else {
                        WriteAudioSample(pW, audIdx, pS, audWritten, params->volumeScale);
                        audWritten += dur;
                        audPosInRange += dur;
                        pS->Release();
                    }
                }
                else {
                    if (pS)pS->Release();
                    eof = true;
                }

                if (eof) {
                    audEOF = true;
                    if (params->audioShortMode == ASM_LOOP) {
                        pAud->Release();
                        // Relancer depuis le point de depart CHOISI
                        pAud = OpenAudioReader(params->audioPath, params->audioStart, preset.audChannels);
                        audPosInRange = 0;
                        if (pAud) audEOF = false;
                    }
                }
            }
            else {
                LONGLONG need = audioTarget - audWritten;
                if (need <= 0) break;
                LONGLONG chunk = std::min(need, 2000000LL);
                LONGLONG written = WriteSilenceChunk(pW, audIdx, audWritten, chunk,
                                                     pcmBytesNum, pcmHnsDen);
                if (written <= 0) break;
                audWritten += written;
            }
        }
    }

    PostMessage(params->hWnd, WM_ENCODE_PROGRESS, 100, 0);
    if (pAud)pAud->Release();
    pVid->Release();
    pW->Finalize();
    pW->Release();
    g.lastError.clear();
    PostMessage(params->hWnd, WM_ENCODE_DONE, 1, 0);
}

// ---------------------------------------------------------------------------
// Dessin waveform
// ---------------------------------------------------------------------------
static void DrawWaveform(HWND hWnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom, mid = H / 2, maxAmp = mid - 4;
    HBRUSH hBg = CreateSolidBrush(CLR_WAVE_BG);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    if (g.waveform.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(80, 80, 120));
        HFONT hf = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, hf);
        std::wstring txt = g.audioPath.empty() ? S(IDS_WAVE_LOAD)
                           : (g.waveformReady ? S(IDS_WAVE_UNAVAIL) : S(IDS_WAVE_ANALYZING));
        DrawText(hdc, txt.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(hf);
        return;
    }
    double dur = g.audioDuration;
    int cxS = (dur > 0) ? (int)(g.audioStartSec / dur * W) : 0;
    int cxE = (dur > 0) ? (int)(g.audioEndSec / dur * W) : W;
    if (cxS > 0) {
        RECT z = { 0,0,cxS,H };
        HBRUSH h = CreateSolidBrush(CLR_ZONE);
        FillRect(hdc, &z, h);
        DeleteObject(h);
    }
    if (cxE > cxS) {
        RECT z = { cxS,0,cxE,H };
        HBRUSH h = CreateSolidBrush(CLR_ZONE_SEL);
        FillRect(hdc, &z, h);
        DeleteObject(h);
    }

    int nb = (int)g.waveform.size();
    for (int i = 0; i < nb; i++) {
        float v = g.waveform[i];
        int x = (int)((float)i / nb * W), x2 = (int)((float)(i + 1) / nb * W), amp = (int)(v * maxAmp);
        int r = std::min(255, 84 + (int)(v * 50)), gv = std::min(255, 104 + (int)(v * 70));
        HPEN hp = CreatePen(PS_SOLID, std::max(1, x2 - x - 1), RGB(r, gv, 255));
        HPEN op = (HPEN)SelectObject(hdc, hp);
        MoveToEx(hdc, x, mid - amp, nullptr);
        LineTo(hdc, x, mid + amp + 1);
        SelectObject(hdc, op);
        DeleteObject(hp);
    }
    HPEN hc = CreatePen(PS_DOT, 1, RGB(60, 60, 90));
    HPEN oc = (HPEN)SelectObject(hdc, hc);
    MoveToEx(hdc, 0, mid, nullptr);
    LineTo(hdc, W, mid);
    SelectObject(hdc, oc);
    DeleteObject(hc);

    auto DrawCursor = [&](int cx, COLORREF col, bool top) {
        HPEN hp = CreatePen(PS_SOLID, 2, col);
        HPEN op = (HPEN)SelectObject(hdc, hp);
        MoveToEx(hdc, cx, 0, nullptr);
        LineTo(hdc, cx, H);
        SelectObject(hdc, op);
        DeleteObject(hp);
        POINT tri[3];
        if (top) tri[0] = { cx - 5,0 }, tri[1] = { cx + 5,0 }, tri[2] = { cx,9 };
        else    tri[0] = { cx - 5,H }, tri[1] = { cx + 5,H }, tri[2] = { cx,H - 9 };
        HBRUSH hb = CreateSolidBrush(col);
        HPEN hn = (HPEN)GetStockObject(NULL_PEN);
        HPEN op2 = (HPEN)SelectObject(hdc, hn);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, hb);
        Polygon(hdc, tri, 3);
        SelectObject(hdc, op2);
        SelectObject(hdc, ob);
        DeleteObject(hb);
    };
    DrawCursor(cxS, CLR_CUR_START, true);
    DrawCursor(cxE, CLR_CUR_END, false);

    if (dur > 0) {
        std::wstring lbl = L"\u25b6 " + SecsToHMS(g.audioStartSec)
                           + L"  \u2192  " + SecsToHMS(g.audioEndSec) + L" / " + SecsToHMS(dur);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(150, 150, 180));
        HFONT hf = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, hf);
        RECT lr = { 0,H - 16,W - 4,H };
        DrawText(hdc, lbl.c_str(), -1, &lr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(hf);
    }
}

// ---------------------------------------------------------------------------
// WndProc spectrogramme
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WaveformWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        DrawWaveform(hWnd, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (g.audioDuration <= 0.0) break;
        RECT rc;
        GetClientRect(hWnd, &rc);
        double ratio = std::max(0.0, std::min(1.0, (double)GET_X_LPARAM(lParam) / rc.right));
        double sec = ratio * g.audioDuration;
        double dS = std::abs(sec - g.audioStartSec), dE = std::abs(sec - g.audioEndSec);
        g.draggingEnd = (dE < dS);
        HWND hP = GetParent(hWnd);
        if (g.draggingEnd) {
            g.audioEndSec = std::max(g.audioStartSec + 0.5, sec);
            SetDlgItemText(hP, ID_EDIT_AUD_END, SecsToHMS(g.audioEndSec).c_str());
        }
        else {
            g.audioStartSec = std::min(sec, g.audioEndSec - 0.5);
            SetDlgItemText(hP, ID_EDIT_AUD_START, SecsToHMS(g.audioStartSec).c_str());
        }
        SetCapture(hWnd);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case WM_MOUSEMOVE: {
        if (!(wParam & MK_LBUTTON) || g.audioDuration <= 0.0) break;
        RECT rc;
        GetClientRect(hWnd, &rc);
        double ratio = std::max(0.0, std::min(1.0, (double)GET_X_LPARAM(lParam) / rc.right));
        double sec = ratio * g.audioDuration;
        HWND hP = GetParent(hWnd);
        if (g.draggingEnd) {
            g.audioEndSec = std::max(g.audioStartSec + 0.5, sec);
            SetDlgItemText(hP, ID_EDIT_AUD_END, SecsToHMS(g.audioEndSec).c_str());
        }
        else {
            g.audioStartSec = std::min(sec, g.audioEndSec - 0.5);
            SetDlgItemText(hP, ID_EDIT_AUD_START, SecsToHMS(g.audioStartSec).c_str());
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Boites de dialogue fichiers
// ---------------------------------------------------------------------------
static std::wstring BrowseFile(HWND hOwner, bool isVideo)
{
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hOwner;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (isVideo) {
        ofn.lpstrFilter = L"Video\0*.mp4;*.mov;*.avi;*.mkv;*.m4v;*.wmv\0All\0*.*\0";
        ofn.lpstrTitle = L"Video";
    }
    else {
        ofn.lpstrFilter = L"Audio\0*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a;*.wma\0All\0*.*\0";
        ofn.lpstrTitle = L"Music";
    }
    return GetOpenFileName(&ofn) ? buf : L"";
}
static std::wstring BrowseSave(HWND hOwner, const std::wstring& def)
{
    wchar_t buf[MAX_PATH * 2] = {};
    wcscpy_s(buf, def.c_str());
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hOwner;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrFilter = L"MP4\0*.mp4\0All\0*.*\0";
    ofn.lpstrDefExt = L"mp4";
    ofn.lpstrTitle = L"Save as";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    return GetSaveFileName(&ofn) ? buf : L"";
}

// ---------------------------------------------------------------------------
// Appliquer chemins (drag-drop / saisie / parcourir)
// ---------------------------------------------------------------------------
static void ApplyVideoPath(HWND hWnd, const std::wstring& p)
{
    if (p.empty() || !PathFileExists(p.c_str())) return;
    g.videoPath = p;
    double dur = MF_GetDuration(p);
    g.videoDuration = dur;
    SetDlgItemText(hWnd, ID_EDIT_VIDEO, p.c_str());
    if (dur > 0) {
        SetDlgItemText(hWnd, ID_EDIT_VID_END, SecsToHMS(dur).c_str());
        SetDlgItemText(hWnd, ID_STATIC_VID_DUR,
                       Sfmt2(IDS_DURATION_FMT, SecsToHMS(dur).c_str(), PathFindFileName(p.c_str())).c_str());
    }
    else SetDlgItemText(hWnd, ID_STATIC_VID_DUR, S(IDS_DUR_UNAVAIL).c_str());
}
static void ApplyAudioPath(HWND hWnd, const std::wstring& p)
{
    if (p.empty() || !PathFileExists(p.c_str())) return;
    g.audioPath = p;
    g.audioStartSec = 0;
    g.waveformReady = false;
    g.waveform.clear();
    double dur = MF_GetDuration(p);
    g.audioDuration = dur;
    g.audioEndSec = (dur > 0) ? dur : 0;
    SetDlgItemText(hWnd, ID_EDIT_AUDIO, p.c_str());
    SetDlgItemText(hWnd, ID_EDIT_AUD_START, L"00:00:00");
    SetDlgItemText(hWnd, ID_EDIT_AUD_END, (dur > 0) ? SecsToHMS(dur).c_str() : L"");
    if (dur > 0)
        SetDlgItemText(hWnd, ID_STATIC_AUD_DUR,
                       Sfmt2(IDS_DURATION_FMT, SecsToHMS(dur).c_str(), PathFindFileName(p.c_str())).c_str());
    InvalidateRect(g.hWaveWnd, nullptr, FALSE);
    std::thread([p, hWnd] {ExtractWaveformThread(p, hWnd); }).detach();
}

// ---------------------------------------------------------------------------
// Helpers creation controles
// ---------------------------------------------------------------------------
static HWND MkL(HWND p, const wchar_t* t, int x, int y, int w, int h, bool b = false)
{
    HWND h2 = CreateWindow(L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(b ? g.hFontBold : g.hFontUI), TRUE);
    return h2;
}
static HWND MkE(HWND p, int id, const wchar_t* t, int x, int y, int w, int h)
{
    HWND h2 = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", t,
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    return h2;
}
static HWND MkB(HWND p, int id, const wchar_t* t, int x, int y, int w, int h, bool ac = false)
{
    HWND h2 = CreateWindow(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | (ac ? BS_DEFPUSHBUTTON : 0),
                           x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(ac ? g.hFontBold : g.hFontUI), TRUE);
    return h2;
}
static HWND MkS(HWND p, int id, const wchar_t* t, int x, int y, int w, int h, HFONT f = nullptr)
{
    HWND h2 = CreateWindow(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(f ? f : g.hFontUI), TRUE);
    return h2;
}
static HWND MkC(HWND p, int id, int x, int y, int w, int h)
{
    HWND h2 = CreateWindow(WC_COMBOBOX, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    return h2;
}

// ---------------------------------------------------------------------------
// WndProc principal
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g.hWnd = hWnd;
        g.hBrushBg = CreateSolidBrush(CLR_BG);
        g.hFontUI = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g.hFontBold = CreateFont(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g.hFontSm = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hFT = CreateFont(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&g.pTaskbar));
        if (g.pTaskbar) g.pTaskbar->HrInit();

        DragAcceptFiles(hWnd, TRUE);

        int y = MARGIN, CW = WINW - MARGIN * 2, BW = 90, EW = CW - BW - 6;

        HWND hTit = MkL(hWnd, (S(IDS_APP_TITLE)).c_str(), MARGIN, y, CW, 24, true);
        SendMessage(hTit, WM_SETFONT, (WPARAM)hFT, TRUE);
        y += 28;
        MkL(hWnd, S(IDS_DROP_HINT).c_str(), MARGIN, y, CW, 18);
        y += 24;

        // Video
        MkL(hWnd, S(IDS_SEC_VIDEO).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        MkE(hWnd, ID_EDIT_VIDEO, L"", MARGIN, y, EW, ROW_H);
        MkB(hWnd, ID_BTN_VIDEO, S(IDS_BROWSE).c_str(), MARGIN + EW + 6, y, BW, ROW_H);
        y += ROW_H + 4;
        MkL(hWnd, S(IDS_CROP_START).c_str(), MARGIN, y + 4, 112, 18);
        MkE(hWnd, ID_EDIT_VID_START, L"00:00:00", MARGIN + 114, y, 80, ROW_H);
        MkL(hWnd, S(IDS_CROP_END).c_str(), MARGIN + 198, y + 4, 38, 18);
        MkE(hWnd, ID_EDIT_VID_END, L"", MARGIN + 238, y, 80, ROW_H);
        MkL(hWnd, S(IDS_CROP_HINT).c_str(), MARGIN + 322, y + 4, 200, 18);
        y += ROW_H + 4;
        MkS(hWnd, ID_STATIC_VID_DUR, L"", MARGIN, y, CW, 16, g.hFontSm);
        y += 22;

        // Musique
        MkL(hWnd, S(IDS_SEC_MUSIC).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        MkE(hWnd, ID_EDIT_AUDIO, L"", MARGIN, y, EW, ROW_H);
        MkB(hWnd, ID_BTN_AUDIO, S(IDS_BROWSE).c_str(), MARGIN + EW + 6, y, BW, ROW_H);
        y += ROW_H + 4;
        MkL(hWnd, S(IDS_MUS_START).c_str(), MARGIN, y + 4, 56, 18);
        MkE(hWnd, ID_EDIT_AUD_START, L"00:00:00", MARGIN + 58, y, 80, ROW_H);
        MkL(hWnd, S(IDS_MUS_END).c_str(), MARGIN + 142, y + 4, 46, 18);
        MkE(hWnd, ID_EDIT_AUD_END, L"", MARGIN + 190, y, 80, ROW_H);
        MkL(hWnd, S(IDS_MUS_WAVE_HINT).c_str(), MARGIN + 274, y + 4, 300, 18);
        y += ROW_H + 6;

        // Spectrogramme
        {
            WNDCLASSEX wcw = {};
            wcw.cbSize = sizeof(wcw);
            wcw.style = CS_HREDRAW | CS_VREDRAW;
            wcw.lpfnWndProc = WaveformWndProc;
            wcw.hInstance = g_hInst;
            wcw.lpszClassName = L"WaveformClass";
            RegisterClassEx(&wcw);
            g.hWaveWnd = CreateWindowEx(WS_EX_CLIENTEDGE, L"WaveformClass", L"",
                                        WS_CHILD | WS_VISIBLE, MARGIN, y, CW, WAVEFORM_H, hWnd, nullptr, g_hInst, nullptr);
        }
        y += WAVEFORM_H + 4;
        MkS(hWnd, ID_STATIC_AUD_DUR, L"", MARGIN, y, CW, 16, g.hFontSm);
        y += 22;

        // Volume
        MkL(hWnd, S(IDS_VOLUME).c_str(), MARGIN, y + 3, 64, 18);
        HWND hSlider = CreateWindow(TRACKBAR_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                    MARGIN + 66, y, 220, ROW_H, hWnd, (HMENU)ID_SLIDER_VOLUME, nullptr, nullptr);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
        SendMessage(hSlider, TBM_SETPOS, TRUE, g_volumePct);
        MkS(hWnd, ID_STATIC_VOL, L"100 %", MARGIN + 290, y + 3, 60, 18, g.hFontSm);
        y += ROW_H + 8;

        // Musique trop courte
        MkL(hWnd, S(IDS_SEC_SHORT).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        HWND hCA = MkC(hWnd, ID_COMBO_AUDSHORT, MARGIN, y, 340, 120);
        SendMessage(hCA, CB_ADDSTRING, 0, (LPARAM)S(IDS_LOOP).c_str());
        SendMessage(hCA, CB_ADDSTRING, 0, (LPARAM)S(IDS_SILENCE).c_str());
        SendMessage(hCA, CB_SETCURSEL, (WPARAM)g_audioShortMode, 0);
        y += ROW_H + 8;

        // Qualite
        MkL(hWnd, S(IDS_SEC_QUALITY).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        HWND hCQ = MkC(hWnd, ID_COMBO_QUALITY, MARGIN, y, 420, 160);
        for (int i = 0; i < N_PRESETS; i++)
            SendMessage(hCQ, CB_ADDSTRING, 0, (LPARAM)S(PRESETS[i].lblId).c_str());
        SendMessage(hCQ, CB_SETCURSEL, (WPARAM)g_qualityIdx, 0);
        y += ROW_H + 4;
        MkS(hWnd, ID_STATIC_QINFO, S(PRESETS[g_qualityIdx].dscId).c_str(), MARGIN, y, CW, 16, g.hFontSm);
        y += 22;

        // GO
        CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                     MARGIN, y, CW, 2, hWnd, nullptr, nullptr, nullptr);
        y += 10;
        MkB(hWnd, ID_BTN_GO, S(IDS_BTN_GO).c_str(), WINW / 2 - 115, y, 230, 36, true);
        y += 46;

        HWND hProg = CreateWindow(PROGRESS_CLASS, L"", WS_CHILD,
                                  MARGIN, y, CW, 14, hWnd, (HMENU)ID_PROGRESS, nullptr, nullptr);
        SendMessage(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hProg, PBM_SETPOS, 0, 0);
        ShowWindow(hProg, SW_HIDE);
        y += 20;
        MkS(hWnd, ID_STATIC_STATUS, S(IDS_READY).c_str(), MARGIN, y, CW, 18, g.hFontSm);
        y += 24;

        RECT wr = { 0,0,WINW,y + MARGIN };
        AdjustWindowRect(&wr, (DWORD)GetWindowLong(hWnd, GWL_STYLE), FALSE);
        SetWindowPos(hWnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT n = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            wchar_t buf[MAX_PATH * 2] = {};
            DragQueryFile(hDrop, i, buf, MAX_PATH * 2);
            std::wstring ext = buf;
            size_t dot = ext.rfind(L'.');
            if (dot != std::wstring::npos) ext = ext.substr(dot + 1);
            for (auto& c : ext) c = towlower(c);
            bool isV = (ext == L"mp4" || ext == L"mov" || ext == L"avi" || ext == L"mkv" || ext == L"m4v" || ext == L"wmv");
            bool isA = (ext == L"mp3" || ext == L"wav" || ext == L"aac" || ext == L"flac" || ext == L"ogg" || ext == L"m4a" || ext == L"wma");
            if (isV) ApplyVideoPath(hWnd, buf);
            else if (isA) ApplyAudioPath(hWnd, buf);
        }
        DragFinish(hDrop);
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        SetBkColor((HDC)wParam, CLR_BG);
        SetTextColor((HDC)wParam, CLR_TEXT);
        return (LRESULT)g.hBrushBg;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g.hBrushBg);
        return 1;
    }
    case WM_HSCROLL: {
        HWND hSl = GetDlgItem(hWnd, ID_SLIDER_VOLUME);
        if ((HWND)lParam == hSl) {
            g_volumePct = (int)SendMessage(hSl, TBM_GETPOS, 0, 0);
            wchar_t buf[16];
            swprintf_s(buf, L"%d %%", g_volumePct);
            SetDlgItemText(hWnd, ID_STATIC_VOL, buf);
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_VIDEO) {
            std::wstring p = BrowseFile(hWnd, true);
            if (!p.empty())ApplyVideoPath(hWnd, p);
        }
        else if (id == ID_BTN_AUDIO) {
            std::wstring p = BrowseFile(hWnd, false);
            if (!p.empty())ApplyAudioPath(hWnd, p);
        }
        else if ((id == ID_EDIT_VIDEO || id == ID_EDIT_AUDIO) && HIWORD(wParam) == EN_KILLFOCUS) {
            std::wstring p = CtrlText(hWnd, id);
            if (!p.empty() && PathFileExists(p.c_str())) {
                if (id == ID_EDIT_VIDEO) ApplyVideoPath(hWnd, p);
                else                  ApplyAudioPath(hWnd, p);
            }
        }
        else if (id == ID_COMBO_QUALITY && HIWORD(wParam) == CBN_SELCHANGE) {
            g_qualityIdx = (int)SendDlgItemMessage(hWnd, ID_COMBO_QUALITY, CB_GETCURSEL, 0, 0);
            SetDlgItemText(hWnd, ID_STATIC_QINFO, S(PRESETS[g_qualityIdx].dscId).c_str());
        }
        else if (id == ID_COMBO_AUDSHORT && HIWORD(wParam) == CBN_SELCHANGE) {
            g_audioShortMode = (AudioShortMode)SendDlgItemMessage(hWnd, ID_COMBO_AUDSHORT, CB_GETCURSEL, 0, 0);
        }
        else if (id == ID_BTN_GO) {
            if (g.encoding) break;
            std::wstring video = CtrlText(hWnd, ID_EDIT_VIDEO);
            std::wstring audio = CtrlText(hWnd, ID_EDIT_AUDIO);

            // Validations fichiers
            if (video.empty() || !PathFileExists(video.c_str()))
            {
                ErrBox(hWnd, IDS_ERR_NO_VIDEO);
                break;
            }
            if (audio.empty() || !PathFileExists(audio.c_str()))
            {
                ErrBox(hWnd, IDS_ERR_NO_AUDIO);
                break;
            }

            // Validations temps
            double audStart = HMSToSecs(CtrlText(hWnd, ID_EDIT_AUD_START));
            std::wstring audEndTxt = CtrlText(hWnd, ID_EDIT_AUD_END);
            double audEnd = audEndTxt.empty() ? 0.0 : HMSToSecs(audEndTxt);
            double vidStart = HMSToSecs(CtrlText(hWnd, ID_EDIT_VID_START));
            std::wstring finTxt = CtrlText(hWnd, ID_EDIT_VID_END);
            double vidEnd = finTxt.empty() ? 0.0 : HMSToSecs(finTxt);

            if (audStart < 0) {
                ErrBox(hWnd, IDS_ERR_BAD_AUD_START);
                break;
            }
            if (vidStart < 0) {
                ErrBox(hWnd, IDS_ERR_BAD_VID_START);
                break;
            }
            // Debut >= fin explicite → erreur
            if (vidEnd > 0.0 && vidStart >= vidEnd) {
                ErrBox(hWnd, IDS_ERR_VID_ORDER);
                break;
            }
            if (audEnd > 0.0 && audStart >= audEnd) {
                ErrBox(hWnd, IDS_ERR_AUD_ORDER);
                break;
            }

            wchar_t base[MAX_PATH];
            wcscpy_s(base, video.c_str());
            PathRemoveExtension(base);
            std::wstring out = BrowseSave(hWnd, std::wstring(PathFindFileName(base)) + L"_music.mp4");
            if (out.empty()) break;

            g.encoding = true;
            EnableWindow(GetDlgItem(hWnd, ID_BTN_GO), FALSE);
            SetDlgItemText(hWnd, ID_STATIC_STATUS, S(IDS_ENCODING).c_str()); // "0 %" sera mis a jour
            HWND hP = GetDlgItem(hWnd, ID_PROGRESS);
            SendMessage(hP, PBM_SETPOS, 0, 0);
            ShowWindow(hP, SW_SHOW);
            TBProgress(0);

            auto* ep = new EncodeParams{ video,audio,out,vidStart,vidEnd,
                                         audStart,audEnd,g_audioShortMode,
                                         g_qualityIdx,g_volumePct / 100.0f,hWnd };
            std::thread(EncodeThread, ep).detach();
        }
        break;
    }

    case WM_ENCODE_PROGRESS: {
        int pct = (int)wParam;
        SendDlgItemMessage(hWnd, ID_PROGRESS, PBM_SETPOS, (WPARAM)pct, 0);
        SetDlgItemText(hWnd, ID_STATIC_STATUS, Sfmt(IDS_ENCODING, pct).c_str());
        TBProgress(pct);
        break;
    }
    case WM_ENCODE_DONE: {
        g.encoding = false;
        EnableWindow(GetDlgItem(hWnd, ID_BTN_GO), TRUE);
        ShowWindow(GetDlgItem(hWnd, ID_PROGRESS), SW_HIDE);
        bool ok = (wParam == 1);
        TBDone(ok);
        if (ok) {
            SetDlgItemText(hWnd, ID_STATIC_STATUS, S(IDS_DONE_STATUS).c_str());
            MessageBox(hWnd, S(IDS_DONE_MSG).c_str(), S(IDS_DONE_TITLE).c_str(), MB_ICONINFORMATION);
        }
        else {
            SetDlgItemText(hWnd, ID_STATIC_STATUS, S(IDS_ERR_STATUS).c_str());
            MessageBox(hWnd, (S(IDS_ERR_TITLE) + L":\n\n" + g.lastError).c_str(),
                       S(IDS_ERR_TITLE).c_str(), MB_ICONERROR);
        }
        break;
    }
    case WM_WAVEFORM_READY: {
        auto* h = (std::vector<float>*)lParam;
        if (h) {
            g.waveform = std::move(*h);
            delete h;
        }
        g.waveformReady = true;
        InvalidateRect(g.hWaveWnd, nullptr, FALSE);
        break;
    }
    case WM_DESTROY:
        DragAcceptFiles(hWnd, FALSE);
        if (g.pTaskbar) {
            g.pTaskbar->Release();
            g.pTaskbar = nullptr;
        }
        DeleteObject(g.hFontUI);
        DeleteObject(g.hFontBold);
        DeleteObject(g.hFontSm);
        DeleteObject(g.hBrushBg);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInst;

    // Detecter la langue de l'utilisateur
    LANGID uiLang = GetUserDefaultUILanguage();
    WORD   primary = PRIMARYLANGID(uiLang);
    // Si la langue systeme n'est pas le francais, utiliser l'anglais
    if (primary != LANG_FRENCH) {
        // Forcer les ressources en anglais
        SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
        g_langId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    }
    else {
        SetThreadUILanguage(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH));
        g_langId = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);
    }

    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    MFStartup(MF_VERSION);
    INITCOMMONCONTROLSEX icc = { sizeof(icc),
                                 ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES
                               };
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"RemplaceurMusique";
    wc.hIcon = LoadIcon(nullptr, MAKEINTRESOURCE(IDI_MY_APP_ICON));
    wc.hIconSm = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_MY_APP_ICON),
                                  IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindow(L"RemplaceurMusique",
                             S(IDS_APP_TITLE).c_str(),
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, WINW, 800,
                             nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    MFShutdown();
    CoUninitialize();
    return (int)msg.wParam;
}