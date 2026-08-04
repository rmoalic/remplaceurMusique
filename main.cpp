// ===========================================================================
//  Video Music Replacer / Remplaceur de Musique Video
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
#include <mfobjects.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <codecapi.h>
#include <wrl/client.h>   // ComPtr
#include "resource.hpp"
#include "Encode.hpp"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <cstring>
#include <iterator>
#include "VideoEncoder.hpp"
#include "EncodeJob.hpp"
#include "WaveformExtractorJob.hpp"

using Microsoft::WRL::ComPtr;
double MF_GetDuration(const std::wstring& path);

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
#pragma comment(lib,"d3d11.lib")

// ---------------------------------------------------------------------------
// Control IDs
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

// ---------------------------------------------------------------------------
// Colours / layout constants
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

// ---------------------------------------------------------------------------
// Localisation
// ---------------------------------------------------------------------------
static HINSTANCE g_hInst = nullptr;
static LANGID    g_langId = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);

static std::wstring LoadStr(UINT id)
{
    wchar_t buf[512] = {};
    if (LoadStringW(g_hInst, id, buf, 512) > 0) return buf;
    return L"";
}
static std::wstring S(UINT id) {
    return LoadStr(id);
}

static std::wstring Sfmt(UINT id, int v)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, LoadStr(id).c_str(), v);
    return buf;
}
static std::wstring Sfmt2(UINT id, const wchar_t* a, const wchar_t* b)
{
    wchar_t buf[512] = {};
    swprintf_s(buf, LoadStr(id).c_str(), a, b);
    return buf;
}

// ---------------------------------------------------------------------------
// Quality presets
// ---------------------------------------------------------------------------
static int            g_qualityIdx = 1;
static AudioShortMode g_audioShortMode = ASM_LOOP;
static int            g_volumePct = 100;

// ===========================================================================
// State — split into UI-only and cross-thread shared
// ===========================================================================

// Owned exclusively by the UI thread — never touched by a worker thread
struct UIState {
    HWND   hWnd = nullptr;
    HWND   hWaveWnd = nullptr;
    HFONT  hFontUI = nullptr;
    HFONT  hFontBold = nullptr;
    HFONT  hFontSm = nullptr;
    HBRUSH hBrushBg = nullptr;
    ComPtr<ITaskbarList3> pTaskbar;

    // File selection
    std::wstring videoPath, audioPath;
    double videoDuration = 0.0;
    double audioDuration = 0.0;

    // Waveform cursors (UI thread only)
    double audioStartSec = 0.0;
    double audioEndSec = 0.0;
    bool   draggingEnd = false;

    // Waveform
    std::vector<float> waveform;
    bool waveformReady = false;
};
static UIState ui;

std::atomic<bool> encoding{ false };
static std::unique_ptr<EncodeJob> g_encodeJob;
static WaveformExtractorJob g_waveform(WAVE_SAMPLES);

struct WaveformResult
{
    uint64_t generation;
    std::vector<float> values;
};

// ---------------------------------------------------------------------------
// ITaskbarList3 helpers
// ---------------------------------------------------------------------------
static void TBProgress(int pct)
{
    if (!ui.pTaskbar || !ui.hWnd) return;
    ui.pTaskbar->SetProgressState(ui.hWnd, TBPF_NORMAL);
    ui.pTaskbar->SetProgressValue(ui.hWnd, (ULONGLONG)pct, 100ULL);
}
static void TBDone()
{
    if (!ui.pTaskbar || !ui.hWnd) return;
    ui.pTaskbar->SetProgressState(ui.hWnd, TBPF_NOPROGRESS);
}
static void TBError()
{
    if (!ui.pTaskbar || !ui.hWnd) return;
    ui.pTaskbar->SetProgressState(ui.hWnd, TBPF_ERROR);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
static std::wstring SecsToHMS(double s)
{
    if (s < 0) s = 0;
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

static std::wstring LocalizeEncodeError(EncodeError err)
{
    switch (err) {
    case EncodeError::VideoOpenFailed:
        return L"Impossible d'ouvrir la vidéo source.";
    case EncodeError::VideoStreamSelectFailed:
        return L"Impossible de sélectionner le flux vidéo source.";
    case EncodeError::VideoDecodeFailed:
        return L"Impossible de décoder la vidéo.";
    case EncodeError::VideoTypeReadFailed:
        return L"Erreur lecture type vidéo.";
    case EncodeError::VideoDimensionsInvalid:
        return L"Dimensions vidéo source invalides.";
    case EncodeError::AudioOpenFailed:
        return L"Impossible d'ouvrir le fichier audio.";
    case EncodeError::AudioStreamSelectFailed:
        return L"Impossible de sélectionner le flux audio source.";
    case EncodeError::AudioFormatFailed:
        return L"Impossible de décoder l'audio au format PCM demandé.";
    case EncodeError::AudioSeekFailed:
        return L"Impossible de positionner l'audio source.";
    case EncodeError::AudioTypeReadFailed:
        return L"Impossible de lire le format PCM audio.";
    case EncodeError::OutputCreateFailed:
        return L"Impossible de créer le fichier de sortie.";
    case EncodeError::VideoStreamAddFailed:
        return L"Erreur ajout flux H264.";
    case EncodeError::VideoStreamTypeIncompatible:
        return L"Type vidéo incompatible avec l'encodeur H264.";
    case EncodeError::AudioStreamAddFailed:
        return L"Erreur ajout flux AAC.";
    case EncodeError::AudioStreamConfigFailed:
        return L"Erreur configuration AAC. (Windows 7+)";
    case EncodeError::VideoSeekFailed:
        return L"Impossible de positionner la vidéo source.";
    case EncodeError::SinkWriterBeginFailed:
        return L"Erreur démarrage écriture MP4.";
    case EncodeError::SinkWriterFinalizeFailed:
        return L"Impossible de finaliser le fichier MP4.";
    case EncodeError::VideoReadFailed:
        return L"Erreur pendant la lecture de la vidéo.";
    case EncodeError::VideoWriteFailed:
        return L"Erreur pendant l'écriture de la vidéo.";
    case EncodeError::AudioReadFailed:
        return L"Erreur pendant la lecture de l'audio.";
    case EncodeError::AudioWriteFailed:
        return L"Erreur pendant l'écriture de l'audio.";
    case EncodeError::AudioSilenceWriteFailed:
        return L"Erreur pendant l'écriture du silence audio.";
    case EncodeError::AudioLoopRestartFailed:
        return L"Impossible de relancer l'audio source.";
    case EncodeError::Cancelled:
        return L"Annulé.";
    case EncodeError::None:
    case EncodeError::Unknown:
    default:
        return L"Erreur inconnue lors de l'encodage.";
    }
}

// ---------------------------------------------------------------------------
// Waveform drawing — reads UIState only
// ---------------------------------------------------------------------------
static void DrawWaveform(HWND hWnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom, mid = H / 2, maxAmp = mid - 4;

    HBRUSH hBg = CreateSolidBrush(CLR_WAVE_BG);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    if (ui.waveform.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(80, 80, 120));
        HFONT hf = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, hf);
        std::wstring txt = ui.audioPath.empty() ? S(IDS_WAVE_LOAD)
                           : (ui.waveformReady ? S(IDS_WAVE_UNAVAIL) : S(IDS_WAVE_ANALYZING));
        DrawText(hdc, txt.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(hf);
        return;
    }

    double dur = ui.audioDuration;
    int cxS = (dur > 0) ? (int)(ui.audioStartSec / dur * W) : 0;
    int cxE = (dur > 0) ? (int)(ui.audioEndSec / dur * W) : W;

    if (cxS > 0) {
        RECT z = { 0, 0, cxS, H };
        HBRUSH h = CreateSolidBrush(CLR_ZONE);
        FillRect(hdc, &z, h);
        DeleteObject(h);
    }
    if (cxE > cxS) {
        RECT z = { cxS, 0, cxE, H };
        HBRUSH h = CreateSolidBrush(CLR_ZONE_SEL);
        FillRect(hdc, &z, h);
        DeleteObject(h);
    }

    int nb = (int)ui.waveform.size();
    for (int i = 0; i < nb; i++) {
        float v = ui.waveform[i];
        int x = (int)((float)i / nb * W);
        int x2 = (int)((float)(i + 1) / nb * W);
        int amp = (int)(v * maxAmp);
        int r = std::min(255, 84 + (int)(v * 50));
        int gv = std::min(255, 104 + (int)(v * 70));
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
        else     tri[0] = { cx - 5,H }, tri[1] = { cx + 5,H }, tri[2] = { cx,H - 9 };
        HBRUSH hb = CreateSolidBrush(col);
        HPEN   hn = (HPEN)GetStockObject(NULL_PEN);
        HPEN   op2 = (HPEN)SelectObject(hdc, hn);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, hb);
        Polygon(hdc, tri, 3);
        SelectObject(hdc, op2);
        SelectObject(hdc, ob);
        DeleteObject(hb);
    };
    DrawCursor(cxS, CLR_CUR_START, true);
    DrawCursor(cxE, CLR_CUR_END, false);

    if (dur > 0) {
        std::wstring lbl = L"\u25b6 " + SecsToHMS(ui.audioStartSec)
                           + L"  \u2192  " + SecsToHMS(ui.audioEndSec)
                           + L" / " + SecsToHMS(dur);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(150, 150, 180));
        HFONT hf = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, hf);
        RECT lr = { 0, H - 16, W - 4, H };
        DrawText(hdc, lbl.c_str(), -1, &lr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(hf);
    }
}

// ---------------------------------------------------------------------------
// Waveform window proc
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
        if (ui.audioDuration <= 0.0) break;
        RECT rc;
        GetClientRect(hWnd, &rc);
        double ratio = std::max(0.0, std::min(1.0, (double)GET_X_LPARAM(lParam) / rc.right));
        double sec = ratio * ui.audioDuration;
        double dS = std::abs(sec - ui.audioStartSec);
        double dE = std::abs(sec - ui.audioEndSec);
        ui.draggingEnd = (dE < dS);
        HWND hP = GetParent(hWnd);
        if (ui.draggingEnd) {
            ui.audioEndSec = std::max(ui.audioStartSec + 0.5, sec);
            SetDlgItemText(hP, ID_EDIT_AUD_END, SecsToHMS(ui.audioEndSec).c_str());
        }
        else {
            ui.audioStartSec = std::min(sec, ui.audioEndSec - 0.5);
            SetDlgItemText(hP, ID_EDIT_AUD_START, SecsToHMS(ui.audioStartSec).c_str());
        }
        SetCapture(hWnd);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }
    case WM_MOUSEMOVE: {
        if (!(wParam & MK_LBUTTON) || ui.audioDuration <= 0.0) break;
        RECT rc;
        GetClientRect(hWnd, &rc);
        double ratio = std::max(0.0, std::min(1.0, (double)GET_X_LPARAM(lParam) / rc.right));
        double sec = ratio * ui.audioDuration;
        HWND hP = GetParent(hWnd);
        if (ui.draggingEnd) {
            ui.audioEndSec = std::max(ui.audioStartSec + 0.5, sec);
            SetDlgItemText(hP, ID_EDIT_AUD_END, SecsToHMS(ui.audioEndSec).c_str());
        }
        else {
            ui.audioStartSec = std::min(sec, ui.audioEndSec - 0.5);
            SetDlgItemText(hP, ID_EDIT_AUD_START, SecsToHMS(ui.audioStartSec).c_str());
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
// File dialogs
// ---------------------------------------------------------------------------
static std::wstring BrowseFile(HWND hOwner, bool isVideo)
{
    constexpr DWORD kDialogPathChars = 32768;
    std::vector<wchar_t> buf(kDialogPathChars, L'\0');
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hOwner;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = kDialogPathChars;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (isVideo) {
        ofn.lpstrFilter = L"Video\0*.mp4;*.mov;*.avi;*.mkv;*.m4v;*.wmv\0All\0*.*\0";
        ofn.lpstrTitle = L"Video";
    }
    else {
        ofn.lpstrFilter = L"Audio\0*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a;*.wma\0All\0*.*\0";
        ofn.lpstrTitle = L"Music";
    }
    return GetOpenFileName(&ofn) ? buf.data() : L"";
}

static bool SameFilePath(const std::wstring& a, const std::wstring& b)
{
    wchar_t fullA[32768] = {}, fullB[32768] = {};
    DWORD lenA = GetFullPathNameW(a.c_str(), (DWORD)std::size(fullA), fullA, nullptr);
    DWORD lenB = GetFullPathNameW(b.c_str(), (DWORD)std::size(fullB), fullB, nullptr);
    if (lenA == 0 || lenA >= std::size(fullA) || lenB == 0 || lenB >= std::size(fullB)) return false;
    return CompareStringOrdinal(fullA, -1, fullB, -1, TRUE) == CSTR_EQUAL;
}

static std::wstring BrowseSave(HWND hOwner, const std::wstring& def)
{
    constexpr DWORD kDialogPathChars = 32768;
    std::vector<wchar_t> buf(kDialogPathChars, L'\0');
    wcsncpy_s(buf.data(), buf.size(), def.c_str(), _TRUNCATE);
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hOwner;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = kDialogPathChars;
    ofn.lpstrFilter = L"MP4\0*.mp4\0All\0*.*\0";
    ofn.lpstrDefExt = L"mp4";
    ofn.lpstrTitle = L"Save as";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    return GetSaveFileName(&ofn) ? buf.data() : L"";
}

// ---------------------------------------------------------------------------
// Apply paths (drag-drop, typed, browse)
// ---------------------------------------------------------------------------
static void ApplyVideoPath(HWND hWnd, const std::wstring& p)
{
    if (p.empty() || !PathFileExists(p.c_str())) return;
    ui.videoPath = p;
    double dur = MF_GetDuration(p);
    ui.videoDuration = dur;
    SetDlgItemText(hWnd, ID_EDIT_VIDEO, p.c_str());
    if (dur > 0) {
        SetDlgItemText(hWnd, ID_EDIT_VID_END, SecsToHMS(dur).c_str());
        SetDlgItemText(hWnd, ID_STATIC_VID_DUR,
                       Sfmt2(IDS_DURATION_FMT, SecsToHMS(dur).c_str(), PathFindFileName(p.c_str())).c_str());
    }
    else {
        SetDlgItemText(hWnd, ID_STATIC_VID_DUR, S(IDS_DUR_UNAVAIL).c_str());
    }
}
static void ApplyAudioPath(HWND hWnd, const std::wstring& p)
{
    if (p.empty() || !PathFileExists(p.c_str())) return;
    ui.audioPath = p;
    ui.audioStartSec = 0;
    ui.waveformReady = false;
    ui.waveform.clear();
    double dur = MF_GetDuration(p);
    ui.audioDuration = dur;
    ui.audioEndSec = (dur > 0) ? dur : 0;
    SetDlgItemText(hWnd, ID_EDIT_AUDIO, p.c_str());
    SetDlgItemText(hWnd, ID_EDIT_AUD_START, L"00:00:00");
    SetDlgItemText(hWnd, ID_EDIT_AUD_END, (dur > 0) ? SecsToHMS(dur).c_str() : L"");
    if (dur > 0)
        SetDlgItemText(hWnd, ID_STATIC_AUD_DUR,
                       Sfmt2(IDS_DURATION_FMT, SecsToHMS(dur).c_str(), PathFindFileName(p.c_str())).c_str());
    InvalidateRect(ui.hWaveWnd, nullptr, FALSE);

    g_waveform.Start(p, dur, [hWnd](uint64_t generation, std::vector<float> values) {
        auto* result = values.empty() ? nullptr : new WaveformResult{ generation, std::move(values) };
        if (!PostMessage(hWnd, WM_WAVEFORM_READY, (WPARAM)generation, (LPARAM)result))
            delete result;
    });
}

// ---------------------------------------------------------------------------
// Control factory helpers
// ---------------------------------------------------------------------------
static HWND MkL(HWND p, const wchar_t* t, int x, int y, int w, int h, bool b = false)
{
    HWND h2 = CreateWindow(L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, h, p, nullptr, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(b ? ui.hFontBold : ui.hFontUI), TRUE);
    return h2;
}
static HWND MkE(HWND p, int id, const wchar_t* t, int x, int y, int w, int h)
{
    HWND h2 = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", t,
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)ui.hFontUI, TRUE);
    return h2;
}
static HWND MkB(HWND p, int id, const wchar_t* t, int x, int y, int w, int h, bool ac = false)
{
    HWND h2 = CreateWindow(L"BUTTON", t,
                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | (ac ? BS_DEFPUSHBUTTON : 0),
                           x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(ac ? ui.hFontBold : ui.hFontUI), TRUE);
    return h2;
}
static HWND MkS(HWND p, int id, const wchar_t* t, int x, int y, int w, int h, HFONT f = nullptr)
{
    HWND h2 = CreateWindow(L"STATIC", t, WS_CHILD | WS_VISIBLE,
                           x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(f ? f : ui.hFontUI), TRUE);
    return h2;
}
static HWND MkC(HWND p, int id, int x, int y, int w, int h)
{
    HWND h2 = CreateWindow(WC_COMBOBOX, L"",
                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)ui.hFontUI, TRUE);
    return h2;
}

// ---------------------------------------------------------------------------
// Main window proc
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        ui.hWnd = hWnd;
        ui.hBrushBg = CreateSolidBrush(CLR_BG);
        ui.hFontUI = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        ui.hFontBold = CreateFont(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        ui.hFontSm = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hFT = CreateFont(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&ui.pTaskbar));
        if (ui.pTaskbar) ui.pTaskbar->HrInit();

        DragAcceptFiles(hWnd, TRUE);

        int y = MARGIN, CW = WINW - MARGIN * 2, BW = 90, EW = CW - BW - 6;

        HWND hTit = MkL(hWnd, S(IDS_APP_TITLE).c_str(), MARGIN, y, CW, 24, true);
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
        MkE(hWnd, ID_EDIT_VID_START, L"00:00:00", MARGIN + 58, y, 80, ROW_H);
        MkL(hWnd, S(IDS_CROP_END).c_str(), MARGIN + 142, y + 4, 38, 18);
        MkE(hWnd, ID_EDIT_VID_END, L"__:__:__", MARGIN + 190, y, 80, ROW_H);
        MkL(hWnd, S(IDS_CROP_HINT).c_str(), MARGIN + 274, y + 4, 200, 18);
        y += ROW_H + 4;
        MkS(hWnd, ID_STATIC_VID_DUR, L"", MARGIN, y, CW, 16, ui.hFontSm);
        y += 22;

        // Music
        MkL(hWnd, S(IDS_SEC_MUSIC).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        MkE(hWnd, ID_EDIT_AUDIO, L"", MARGIN, y, EW, ROW_H);
        MkB(hWnd, ID_BTN_AUDIO, S(IDS_BROWSE).c_str(), MARGIN + EW + 6, y, BW, ROW_H);
        y += ROW_H + 4;
        MkL(hWnd, S(IDS_CROP_START).c_str(), MARGIN, y + 4, 56, 18);
        MkE(hWnd, ID_EDIT_AUD_START, L"00:00:00", MARGIN + 58, y, 80, ROW_H);
        MkL(hWnd, S(IDS_CROP_END).c_str(), MARGIN + 142, y + 4, 46, 18);
        MkE(hWnd, ID_EDIT_AUD_END, L"__:__:__", MARGIN + 190, y, 80, ROW_H);
        MkL(hWnd, S(IDS_MUS_WAVE_HINT).c_str(), MARGIN + 274, y + 4, 300, 18);
        y += ROW_H + 6;

        // Waveform window (own WNDCLASS so it receives mouse messages)
        {
            WNDCLASSEX wcw = {};
            wcw.cbSize = sizeof(wcw);
            wcw.style = CS_HREDRAW | CS_VREDRAW;
            wcw.lpfnWndProc = WaveformWndProc;
            wcw.hInstance = g_hInst;
            wcw.lpszClassName = L"WaveformClass";
            RegisterClassEx(&wcw);
            ui.hWaveWnd = CreateWindowEx(WS_EX_CLIENTEDGE, L"WaveformClass", L"",
                                         WS_CHILD | WS_VISIBLE, MARGIN, y, CW, WAVEFORM_H, hWnd, nullptr, g_hInst, nullptr);
        }
        y += WAVEFORM_H + 4;
        MkS(hWnd, ID_STATIC_AUD_DUR, L"", MARGIN, y, CW, 16, ui.hFontSm);
        y += 22;

        // Volume slider
        MkL(hWnd, S(IDS_VOLUME).c_str(), MARGIN, y + 3, 64, 18);
        HWND hSlider = CreateWindow(TRACKBAR_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                    MARGIN + 66, y, 220, ROW_H, hWnd, (HMENU)ID_SLIDER_VOLUME, nullptr, nullptr);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
        SendMessage(hSlider, TBM_SETPOS, TRUE, g_volumePct);
        MkS(hWnd, ID_STATIC_VOL, L"100 %", MARGIN + 290, y + 3, 60, 18, ui.hFontSm);
        y += ROW_H + 8;

        // Short-music behaviour
        MkL(hWnd, S(IDS_SEC_SHORT).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        HWND hCA = MkC(hWnd, ID_COMBO_AUDSHORT, MARGIN, y, 340, 120);
        SendMessage(hCA, CB_ADDSTRING, 0, (LPARAM)S(IDS_LOOP).c_str());
        SendMessage(hCA, CB_ADDSTRING, 0, (LPARAM)S(IDS_SILENCE).c_str());
        SendMessage(hCA, CB_SETCURSEL, (WPARAM)g_audioShortMode, 0);
        y += ROW_H + 8;

        // Quality
        MkL(hWnd, S(IDS_SEC_QUALITY).c_str(), MARGIN, y, CW, 20, true);
        y += 22;
        HWND hCQ = MkC(hWnd, ID_COMBO_QUALITY, MARGIN, y, 420, 160);
        for (int i = 0; i < N_PRESETS; i++)
            SendMessage(hCQ, CB_ADDSTRING, 0, (LPARAM)S(PRESETS[i].lblId).c_str());
        SendMessage(hCQ, CB_SETCURSEL, (WPARAM)g_qualityIdx, 0);
        y += ROW_H + 4;
        MkS(hWnd, ID_STATIC_QINFO, S(PRESETS[g_qualityIdx].dscId).c_str(),
            MARGIN, y, CW, 16, ui.hFontSm);
        y += 22;

        // Go button
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
        MkS(hWnd, ID_STATIC_STATUS, S(IDS_READY).c_str(), MARGIN, y, CW, 18, ui.hFontSm);
        y += 24;

        RECT wr = { 0, 0, WINW, y + MARGIN };
        AdjustWindowRect(&wr, (DWORD)GetWindowLong(hWnd, GWL_STYLE), FALSE);
        SetWindowPos(hWnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOMOVE | SWP_NOZORDER);
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT n = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            const UINT cch = DragQueryFile(hDrop, i, nullptr, 0);
            std::vector<wchar_t> buf(cch + 1, L'\0');
            DragQueryFile(hDrop, i, buf.data(), cch + 1);
            std::wstring ext = buf.data();
            size_t dot = ext.rfind(L'.');
            if (dot != std::wstring::npos) ext = ext.substr(dot + 1);
            for (auto& c : ext) c = towlower(c);
            bool isV = (ext == L"mp4" || ext == L"mov" || ext == L"avi" || ext == L"mkv" || ext == L"m4v" || ext == L"wmv");
            bool isA = (ext == L"mp3" || ext == L"wav" || ext == L"aac" || ext == L"flac" || ext == L"ogg" || ext == L"m4a" || ext == L"wma");
            if (isV) ApplyVideoPath(hWnd, buf.data());
            else if (isA) ApplyAudioPath(hWnd, buf.data());
        }
        DragFinish(hDrop);
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, CLR_BG);
        SetTextColor((HDC)wParam, CLR_TEXT);
        return (LRESULT)ui.hBrushBg;

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, ui.hBrushBg);
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
        int code = HIWORD(wParam);

        if (id == ID_BTN_VIDEO) {
            std::wstring p = BrowseFile(hWnd, true);
            if (!p.empty()) ApplyVideoPath(hWnd, p);
        }
        else if (id == ID_BTN_AUDIO) {
            std::wstring p = BrowseFile(hWnd, false);
            if (!p.empty()) ApplyAudioPath(hWnd, p);
        }

        if ((id == ID_EDIT_AUD_START || id == ID_EDIT_AUD_END) && code == EN_CHANGE) {
            std::wstring txt = CtrlText(hWnd, id);
            double sec = HMSToSecs(txt);
            if (sec >= 0.0 && sec <= ui.audioDuration) {
                if (id == ID_EDIT_AUD_START) ui.audioStartSec = sec;
                else                          ui.audioEndSec = sec;
                InvalidateRect(ui.hWaveWnd, nullptr, FALSE);
            }
        }
        else if ((id == ID_EDIT_VIDEO || id == ID_EDIT_AUDIO) && code == EN_KILLFOCUS) {
            std::wstring p = CtrlText(hWnd, id);
            if (!p.empty() && PathFileExists(p.c_str())) {
                if (id == ID_EDIT_VIDEO) ApplyVideoPath(hWnd, p);
                else                     ApplyAudioPath(hWnd, p);
            }
        }
        else if (id == ID_COMBO_QUALITY && code == CBN_SELCHANGE) {
            g_qualityIdx = (int)SendDlgItemMessage(hWnd, ID_COMBO_QUALITY, CB_GETCURSEL, 0, 0);
            SetDlgItemText(hWnd, ID_STATIC_QINFO, S(PRESETS[g_qualityIdx].dscId).c_str());
        }
        else if (id == ID_COMBO_AUDSHORT && code == CBN_SELCHANGE) {
            g_audioShortMode = (AudioShortMode)
                               SendDlgItemMessage(hWnd, ID_COMBO_AUDSHORT, CB_GETCURSEL, 0, 0);
        }
        else if (id == ID_BTN_GO) {
            if (encoding) break;

            std::wstring video = CtrlText(hWnd, ID_EDIT_VIDEO);
            std::wstring audio = CtrlText(hWnd, ID_EDIT_AUDIO);

            if (video.empty() || !PathFileExists(video.c_str())) {
                ErrBox(hWnd, IDS_ERR_NO_VIDEO);
                break;
            }
            if (audio.empty() || !PathFileExists(audio.c_str())) {
                ErrBox(hWnd, IDS_ERR_NO_AUDIO);
                break;
            }

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
            if (vidEnd > 0.0 && vidStart >= vidEnd) {
                ErrBox(hWnd, IDS_ERR_VID_ORDER);
                break;
            }
            if (audEnd > 0.0 && audStart >= audEnd) {
                ErrBox(hWnd, IDS_ERR_AUD_ORDER);
                break;
            }

            std::wstring base = video;
            const size_t slash = base.find_last_of(L"\\/");
            const size_t dot = base.find_last_of(L'.');
            if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) base.resize(dot);
            std::wstring out = BrowseSave(hWnd, std::wstring(PathFindFileName(base.c_str())) + L"_music.mp4");
            if (out.empty()) break;
            if (SameFilePath(out, video) || SameFilePath(out, audio)) {
                MessageBox(hWnd, L"Le fichier de sortie doit être différent des fichiers source.",
                           S(IDS_ERR_TITLE).c_str(), MB_ICONWARNING);
                break;
            }

            encoding = true;
            EnableWindow(GetDlgItem(hWnd, ID_BTN_GO), FALSE);
            wchar_t buf[256] = {};
            swprintf_s(buf, LoadStr(IDS_ENCODING).c_str(), 0, L"00:00:00");
            SetDlgItemText(hWnd, ID_STATIC_STATUS, buf);
            HWND hP = GetDlgItem(hWnd, ID_PROGRESS);
            SendMessage(hP, PBM_SETPOS, 0, 0);
            ShowWindow(hP, SW_SHOW);
            TBProgress(0);

            auto ep = std::make_unique<EncodeParams>(EncodeParams{
                video, audio, out, vidStart, vidEnd,
                audStart, audEnd, g_audioShortMode,
                g_qualityIdx, g_volumePct / 100.0f, hWnd
            });

            EncodeCallbacks callbacks;
            callbacks.onProgress = [hWnd](int pct, double etaSecs) {
                PostMessage(hWnd, WM_ENCODE_PROGRESS, (WPARAM)pct, (LPARAM)(LONGLONG)etaSecs);
            };
            callbacks.onDone = [hWnd](bool ok, EncodeErrorInfo error) {
                ENCODE_DONE_MSG doneMsg{ ok, LocalizeEncodeError(error.code) };
                SendMessage(hWnd, WM_ENCODE_DONE, (WPARAM)&doneMsg, 0);
            };
            g_encodeJob = EncodeJob::Start(std::move(ep), std::move(callbacks));
        }
        break;
    }

    case WM_ENCODE_PROGRESS: {
        int    pct = (int)wParam;
        double etaSecs = (double)(LONGLONG)lParam;
        wchar_t buf[256] = {};
        swprintf_s(buf, LoadStr(IDS_ENCODING).c_str(), pct, SecsToHMS(etaSecs).c_str());
        SendDlgItemMessage(hWnd, ID_PROGRESS, PBM_SETPOS, (WPARAM)pct, 0);
        SetDlgItemText(hWnd, ID_STATIC_STATUS, buf);
        TBProgress(pct);
        break;
    }

    case WM_ENCODE_DONE: {
        encoding = false;
        EnableWindow(GetDlgItem(hWnd, ID_BTN_GO), TRUE);
        ShowWindow(GetDlgItem(hWnd, ID_PROGRESS), SW_HIDE);
        ENCODE_DONE_MSG* encMsg = (ENCODE_DONE_MSG*)wParam;
        if (encMsg->ok) {
            TBProgress(100);
            SetDlgItemText(hWnd, ID_STATIC_STATUS, S(IDS_DONE_STATUS).c_str());
            MessageBox(hWnd, S(IDS_DONE_MSG).c_str(), S(IDS_DONE_TITLE).c_str(), MB_ICONINFORMATION);
        }
        else {
            TBError();
            SetDlgItemText(hWnd, ID_STATIC_STATUS, S(IDS_ERR_STATUS).c_str());
            MessageBox(hWnd, (S(IDS_ERR_TITLE) + L":\n\n" + encMsg->error).c_str(),
                       S(IDS_ERR_TITLE).c_str(), MB_ICONERROR);
        }
        TBDone();
        break;
    }

    case WM_WAVEFORM_READY: {
        // Ignore results from a request that's been superseded by a newer
        // Start() call (e.g. the user picked a different audio file while
        // extraction was still running).
        if ((uint64_t)wParam != g_waveform.CurrentGeneration())
            break;

        if (auto* result = (WaveformResult*)lParam) {
            ui.waveform = std::move(result->values);
            delete result;
        }
        ui.waveformReady = true;
        InvalidateRect(ui.hWaveWnd, nullptr, FALSE);
        break;
    }

    case WM_DESTROY:
        g_waveform.RequestStop();
        if (g_encodeJob) g_encodeJob->RequestCancel();
        DragAcceptFiles(hWnd, FALSE);
        ui.pTaskbar.Reset();
        DeleteObject(ui.hFontUI);
        DeleteObject(ui.hFontBold);
        DeleteObject(ui.hFontSm);
        DeleteObject(ui.hBrushBg);
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

    LANGID uiLang = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(uiLang) != LANG_FRENCH) {
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
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MY_APP_ICON));
    wc.hIconSm = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_MY_APP_ICON),
                                  IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindow(L"RemplaceurMusique", S(IDS_APP_TITLE).c_str(),
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
    if (g_encodeJob) g_encodeJob->RequestCancel();
    g_waveform.Join();
    MFShutdown();
    CoUninitialize();
    return (int)msg.wParam;
}