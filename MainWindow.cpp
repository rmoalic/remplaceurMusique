#include "MainWindow.hpp"
#include "VideoEncoder.hpp"
#include "StringUtils.hpp"
#include "FileUtils.hpp"
#include "Win32Utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

// Initialize static members
int MainWindow::s_qualityIdx = 1;
AudioShortMode MainWindow::s_audioShortMode = ASM_LOOP;
int MainWindow::s_volumePct = 100;

// ===========================================================================
// Construction / Destruction
// ===========================================================================

MainWindow::MainWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance)
    , m_waveformJob(kWaveformSamples)
{
}

MainWindow::~MainWindow()
{
    m_waveformJob.RequestStop();
    m_waveformJob.Join();
    
    if (m_encodeJob) {
        m_encodeJob->RequestCancel();
    }
}

// ===========================================================================
// Window Creation and Message Loop
// ===========================================================================

bool MainWindow::Create(int nCmdShow)
{
    // Set language based on system
    LANGID uiLang = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(uiLang) != LANG_FRENCH) {
        SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
        m_langId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    } else {
        SetThreadUILanguage(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH));
        m_langId = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);
    }

    SetProcessDPIAware();
    
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        return false;
    }
    
    MFStartup(MF_VERSION);
    
    INITCOMMONCONTROLSEX icc = { sizeof(icc),
        ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    // Register main window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProcStatic;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"RemplaceurMusique";
    wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_MY_APP_ICON));
    wc.hIconSm = (HICON)LoadImage(m_hInstance, MAKEINTRESOURCE(IDI_MY_APP_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    RegisterClassEx(&wc);

    // Create main window
    HWND hWnd = CreateWindow(
        L"RemplaceurMusique", LoadString(IDS_APP_TITLE).c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, 800,
        nullptr, nullptr, m_hInstance, this);
    
    if (!hWnd) {
        return false;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
    // Store this pointer in window user data
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    m_ui.hWnd = hWnd;
    
    return true;
}

void MainWindow::RunMessageLoop()
{
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    if (m_encodeJob) {
        m_encodeJob->RequestCancel();
    }
    
    MFShutdown();
    CoUninitialize();
}

// ===========================================================================
// Window Procedures
// ===========================================================================

LRESULT CALLBACK MainWindow::WindowProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    
    if (pThis) {
        return pThis->WindowProc(hWnd, msg, wParam, lParam);
    }
    
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE:
            return OnCreate(hWnd, wParam, lParam);
            
        case WM_DROPFILES:
            OnDropFiles((HDROP)wParam);
            break;
            
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            return OnControlColor((HDC)wParam);
            
        case WM_ERASEBKGND:
            return OnEraseBackground((HDC)wParam);
            
        case WM_HSCROLL:
            OnHScroll((HWND)lParam);
            break;
            
        case WM_COMMAND:
            return OnCommand(hWnd, wParam, lParam);
            
        case WM_ENCODE_PROGRESS: {
            int pct = (int)wParam;
            double etaSecs = (double)(LONGLONG)lParam;
            OnEncodeProgress(pct, etaSecs);
            break;
        }
        
        case WM_ENCODE_DONE:
            OnEncodeDone((ENCODE_DONE_MSG*)wParam);
            break;
            
        case WM_WAVEFORM_READY:
            OnWaveformReady((uint64_t)wParam, (WaveformResult*)lParam);
            break;
            
        case WM_DESTROY:
            OnDestroy();
            break;
    }
    
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::WaveformWindowProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    
    if (pThis) {
        return pThis->WaveformWindowProc(hWnd, msg, wParam, lParam);
    }
    
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::WaveformWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
            if (m_ui.audioDuration <= 0.0) break;
            RECT rc;
            GetClientRect(hWnd, &rc);
            double ratio = std::max(0.0, std::min(1.0, (double)GET_X_LPARAM(lParam) / rc.right));
            double sec = ratio * m_ui.audioDuration;
            double dS = std::abs(sec - m_ui.audioStartSec);
            double dE = std::abs(sec - m_ui.audioEndSec);
            m_ui.draggingEnd = (dE < dS);
            
            if (m_ui.draggingEnd) {
                m_ui.audioEndSec = std::max(m_ui.audioStartSec + 0.5, sec);
                SetDlgItemText(m_ui.hWnd, ID_EDIT_AUD_END, SecondsToHMS(m_ui.audioEndSec).c_str());
            } else {
                m_ui.audioStartSec = std::min(sec, m_ui.audioEndSec - 0.5);
                SetDlgItemText(m_ui.hWnd, ID_EDIT_AUD_START, SecondsToHMS(m_ui.audioStartSec).c_str());
            }
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        
        case WM_MOUSEMOVE: {
            if (!(wParam & MK_LBUTTON) || m_ui.audioDuration <= 0.0) break;
            RECT rc;
            GetClientRect(hWnd, &rc);
            double ratio = std::max(0.0, std::min(1.0, (double)GET_X_LPARAM(lParam) / rc.right));
            double sec = ratio * m_ui.audioDuration;
            
            if (m_ui.draggingEnd) {
                m_ui.audioEndSec = std::max(m_ui.audioStartSec + 0.5, sec);
                SetDlgItemText(m_ui.hWnd, ID_EDIT_AUD_END, SecondsToHMS(m_ui.audioEndSec).c_str());
            } else {
                m_ui.audioStartSec = std::min(sec, m_ui.audioEndSec - 0.5);
                SetDlgItemText(m_ui.hWnd, ID_EDIT_AUD_START, SecondsToHMS(m_ui.audioStartSec).c_str());
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

// ===========================================================================
// Message Handlers: Creation and Destruction
// ===========================================================================

LRESULT MainWindow::OnCreate(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    // Initialize UI state
    m_ui.hWnd = hWnd;
    m_ui.hBrushBg = CreateSolidBrush(kColorBg);
    m_ui.hFontUI = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    m_ui.hFontBold = CreateFont(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    m_ui.hFontSm = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    
    HFONT hFT = CreateFont(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    // Initialize taskbar
    CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_ui.pTaskbar));
    if (m_ui.pTaskbar) {
        m_ui.pTaskbar->HrInit();
    }

    DragAcceptFiles(hWnd, TRUE);

    int y = kMargin, CW = kWindowWidth - kMargin * 2, BW = 90, EW = CW - BW - 6;

    // Title
    HWND hTit = CreateLabel(LoadString(IDS_APP_TITLE).c_str(), kMargin, y, CW, 24, true);
    SendMessage(hTit, WM_SETFONT, (WPARAM)hFT, TRUE);
    y += 28;
    CreateLabel(LoadString(IDS_DROP_HINT).c_str(), kMargin, y, CW, 18);
    y += 24;

    // Video section
    CreateLabel(LoadString(IDS_SEC_VIDEO).c_str(), kMargin, y, CW, 20, true);
    y += 22;
    CreateEdit(ID_EDIT_VIDEO, L"", kMargin, y, EW, kRowHeight);
    CreateButton(ID_BTN_VIDEO, LoadString(IDS_BROWSE).c_str(), kMargin + EW + 6, y, BW, kRowHeight);
    y += kRowHeight + 4;
    CreateLabel(LoadString(IDS_CROP_START).c_str(), kMargin, y + 4, 112, 18);
    CreateEdit(ID_EDIT_VID_START, L"00:00:00", kMargin + 58, y, 80, kRowHeight);
    CreateLabel(LoadString(IDS_CROP_END).c_str(), kMargin + 142, y + 4, 38, 18);
    CreateEdit(ID_EDIT_VID_END, L"__:__:__", kMargin + 190, y, 80, kRowHeight);
    CreateLabel(LoadString(IDS_CROP_HINT).c_str(), kMargin + 274, y + 4, 200, 18);
    y += kRowHeight + 4;
    CreateStatic(ID_STATIC_VID_DUR, L"", kMargin, y, CW, 16, m_ui.hFontSm);
    y += 22;

    // Music section
    CreateLabel(LoadString(IDS_SEC_MUSIC).c_str(), kMargin, y, CW, 20, true);
    y += 22;
    CreateEdit(ID_EDIT_AUDIO, L"", kMargin, y, EW, kRowHeight);
    CreateButton(ID_BTN_AUDIO, LoadString(IDS_BROWSE).c_str(), kMargin + EW + 6, y, BW, kRowHeight);
    y += kRowHeight + 4;
    CreateLabel(LoadString(IDS_CROP_START).c_str(), kMargin, y + 4, 56, 18);
    CreateEdit(ID_EDIT_AUD_START, L"00:00:00", kMargin + 58, y, 80, kRowHeight);
    CreateLabel(LoadString(IDS_CROP_END).c_str(), kMargin + 142, y + 4, 46, 18);
    CreateEdit(ID_EDIT_AUD_END, L"__:__:__", kMargin + 190, y, 80, kRowHeight);
    CreateLabel(LoadString(IDS_MUS_WAVE_HINT).c_str(), kMargin + 274, y + 4, 300, 18);
    y += kRowHeight + 6;

    // Waveform window
    {
        WNDCLASSEX wcw = {};
        wcw.cbSize = sizeof(wcw);
        wcw.style = CS_HREDRAW | CS_VREDRAW;
        wcw.lpfnWndProc = WaveformWindowProcStatic;
        wcw.hInstance = m_hInstance;
        wcw.lpszClassName = L"WaveformClass";
        RegisterClassEx(&wcw);
        
        m_ui.hWaveWnd = CreateWindowEx(WS_EX_CLIENTEDGE, L"WaveformClass", L"",
            WS_CHILD | WS_VISIBLE, kMargin, y, CW, kWaveformHeight, hWnd, nullptr, m_hInstance, this);
        
        // Store MainWindow pointer in waveform window user data
        SetWindowLongPtr(m_ui.hWaveWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }
    y += kWaveformHeight + 4;
    CreateStatic(ID_STATIC_AUD_DUR, L"", kMargin, y, CW, 16, m_ui.hFontSm);
    y += 22;

    // Volume slider
    CreateLabel(LoadString(IDS_VOLUME).c_str(), kMargin, y + 3, 64, 18);
    HWND hSlider = CreateWindow(TRACKBAR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        kMargin + 66, y, 220, kRowHeight, hWnd, (HMENU)(INT_PTR)ID_SLIDER_VOLUME, m_hInstance, nullptr);
    SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
    SendMessage(hSlider, TBM_SETPOS, TRUE, s_volumePct);
    CreateStatic(ID_STATIC_VOL, L"100 %", kMargin + 290, y + 3, 60, 18, m_ui.hFontSm);
    y += kRowHeight + 8;

    // Short audio behavior
    CreateLabel(LoadString(IDS_SEC_SHORT).c_str(), kMargin, y, CW, 20, true);
    y += 22;
    HWND hCA = CreateComboBox(ID_COMBO_AUDSHORT, kMargin, y, 340, 120);
    SendMessage(hCA, CB_ADDSTRING, 0, (LPARAM)LoadString(IDS_LOOP).c_str());
    SendMessage(hCA, CB_ADDSTRING, 0, (LPARAM)LoadString(IDS_SILENCE).c_str());
    SendMessage(hCA, CB_SETCURSEL, (WPARAM)s_audioShortMode, 0);
    y += kRowHeight + 8;

    // Quality
    CreateLabel(LoadString(IDS_SEC_QUALITY).c_str(), kMargin, y, CW, 20, true);
    y += 22;
    HWND hCQ = CreateComboBox(ID_COMBO_QUALITY, kMargin, y, 420, 160);
    for (int i = 0; i < N_PRESETS; i++) {
        SendMessage(hCQ, CB_ADDSTRING, 0, (LPARAM)LoadString(PRESETS[i].lblId).c_str());
    }
    SendMessage(hCQ, CB_SETCURSEL, (WPARAM)s_qualityIdx, 0);
    y += kRowHeight + 4;
    CreateStatic(ID_STATIC_QINFO, LoadString(PRESETS[s_qualityIdx].dscId).c_str(),
        kMargin, y, CW, 16, m_ui.hFontSm);
    y += 22;

    // Go button
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        kMargin, y, CW, 2, hWnd, nullptr, m_hInstance, nullptr);
    y += 10;
    CreateButton(ID_BTN_GO, LoadString(IDS_BTN_GO).c_str(), kWindowWidth / 2 - 115, y, 230, 36, true);
    y += 46;

    // Progress bar
    HWND hProg = CreateWindow(PROGRESS_CLASS, L"", WS_CHILD,
        kMargin, y, CW, 14, hWnd, (HMENU)(INT_PTR)ID_PROGRESS, m_hInstance, nullptr);
    SendMessage(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(hProg, PBM_SETPOS, 0, 0);
    ShowWindow(hProg, SW_HIDE);
    y += 20;
    CreateStatic(ID_STATIC_STATUS, LoadString(IDS_READY).c_str(), kMargin, y, CW, 18, m_ui.hFontSm);
    y += 24;

    // Adjust window size
    RECT wr = { 0, 0, kWindowWidth, y + kMargin };
    AdjustWindowRect(&wr, (DWORD)GetWindowLong(hWnd, GWL_STYLE), FALSE);
    SetWindowPos(hWnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
        SWP_NOMOVE | SWP_NOZORDER);

    DeleteObject(hFT);
    
    return 0;
}

void MainWindow::OnDestroy()
{
    m_waveformJob.RequestStop();
    if (m_encodeJob) {
        m_encodeJob->RequestCancel();
    }
    DragAcceptFiles(m_ui.hWnd, FALSE);
    m_ui.pTaskbar.Reset();
    DeleteObject(m_ui.hFontUI);
    DeleteObject(m_ui.hFontBold);
    DeleteObject(m_ui.hFontSm);
    DeleteObject(m_ui.hBrushBg);
    PostQuitMessage(0);
}

// ===========================================================================
// Message Handlers: Commands
// ===========================================================================

LRESULT MainWindow::OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);

    switch (id) {
        case ID_BTN_VIDEO:
            OnBrowseVideo();
            break;
            
        case ID_BTN_AUDIO:
            OnBrowseAudio();
            break;
            
        case ID_EDIT_AUD_START:
        case ID_EDIT_AUD_END:
            if (code == EN_CHANGE) {
                OnAudioTimeEdit(id);
            }
            break;
            
        case ID_EDIT_VIDEO:
        case ID_EDIT_AUDIO:
            if (code == EN_KILLFOCUS) {
                OnPathEditKillFocus(id);
            }
            break;
            
        case ID_COMBO_QUALITY:
            if (code == CBN_SELCHANGE) {
                OnQualitySelectionChanged();
            }
            break;
            
        case ID_COMBO_AUDSHORT:
            if (code == CBN_SELCHANGE) {
                OnAudioShortModeChanged();
            }
            break;
            
        case ID_BTN_GO:
            OnReplaceAudio();
            break;
    }
    
    return 0;
}

void MainWindow::OnBrowseVideo()
{
    std::wstring path = BrowseFile(true);
    if (!path.empty()) {
        ApplyVideoPath(path);
    }
}

void MainWindow::OnBrowseAudio()
{
    std::wstring path = BrowseFile(false);
    if (!path.empty()) {
        ApplyAudioPath(path);
    }
}

void MainWindow::OnReplaceAudio()
{
    if (m_encoding) return;

    std::wstring video = GetControlText(m_ui.hWnd, ID_EDIT_VIDEO);
    std::wstring audio = GetControlText(m_ui.hWnd, ID_EDIT_AUDIO);

    if (video.empty() || !FileUtils::FileExists(video)) {
        ShowError(IDS_ERR_NO_VIDEO);
        return;
    }
    if (audio.empty() || !FileUtils::FileExists(audio)) {
        ShowError(IDS_ERR_NO_AUDIO);
        return;
    }

    double audStart = HMSToSeconds(GetControlText(m_ui.hWnd, ID_EDIT_AUD_START));
    std::wstring audEndTxt = GetControlText(m_ui.hWnd, ID_EDIT_AUD_END);
    double audEnd = audEndTxt.empty() ? 0.0 : HMSToSeconds(audEndTxt);
    double vidStart = HMSToSeconds(GetControlText(m_ui.hWnd, ID_EDIT_VID_START));
    std::wstring finTxt = GetControlText(m_ui.hWnd, ID_EDIT_VID_END);
    double vidEnd = finTxt.empty() ? 0.0 : HMSToSeconds(finTxt);

    if (audStart < 0) {
        ShowError(IDS_ERR_BAD_AUD_START);
        return;
    }
    if (vidStart < 0) {
        ShowError(IDS_ERR_BAD_VID_START);
        return;
    }
    if (vidEnd > 0.0 && vidStart >= vidEnd) {
        ShowError(IDS_ERR_VID_ORDER);
        return;
    }
    if (audEnd > 0.0 && audStart >= audEnd) {
        ShowError(IDS_ERR_AUD_ORDER);
        return;
    }

    std::wstring base = StringUtils::GetFileNameWithoutExtension(video);
    std::wstring out = BrowseSave(base + L"_music.mp4");
    if (out.empty()) return;
    
    if (ArePathsEqual(out, video) || ArePathsEqual(out, audio)) {
        Win32Utils::MessageBox(m_ui.hWnd, L"Le fichier de sortie doit être différent des fichiers source.",
            LoadString(IDS_ERR_TITLE).c_str(), MB_ICONWARNING);
        return;
    }

    m_encoding = true;
    EnableWindow(GetDlgItem(m_ui.hWnd, ID_BTN_GO), FALSE);
    
    wchar_t buf[256] = {};
    swprintf_s(buf, LoadString(IDS_ENCODING).c_str(), 0, SecondsToHMS(0.0).c_str());
    SetDlgItemText(m_ui.hWnd, ID_STATIC_STATUS, buf);
    
    HWND hP = GetDlgItem(m_ui.hWnd, ID_PROGRESS);
    SendMessage(hP, PBM_SETPOS, 0, 0);
    ShowWindow(hP, SW_SHOW);
    UpdateTaskbarProgress(0);

    auto ep = std::make_unique<EncodeParams>(EncodeParams{
        video, audio, out, vidStart, vidEnd,
        audStart, audEnd, s_audioShortMode,
        s_qualityIdx, s_volumePct / 100.0f, m_ui.hWnd
    });

    EncodeCallbacks callbacks;
    callbacks.onProgress = [this](int pct, double etaSecs) {
        PostMessage(m_ui.hWnd, WM_ENCODE_PROGRESS, (WPARAM)pct, (LPARAM)(LONGLONG)etaSecs);
    };
    callbacks.onDone = [this](bool ok, EncodeErrorInfo error) {
        ENCODE_DONE_MSG doneMsg{ ok, LocalizeEncodeError(error.code) };
        SendMessage(m_ui.hWnd, WM_ENCODE_DONE, (WPARAM)&doneMsg, 0);
    };
    
    m_encodeJob = EncodeJob::Start(std::move(ep), std::move(callbacks));
}

void MainWindow::OnAudioTimeEdit(int controlId)
{
    std::wstring txt = GetControlText(m_ui.hWnd, controlId);
    double sec = HMSToSeconds(txt);
    if (sec >= 0.0 && sec <= m_ui.audioDuration) {
        if (controlId == ID_EDIT_AUD_START) {
            m_ui.audioStartSec = sec;
        } else {
            m_ui.audioEndSec = sec;
        }
        InvalidateRect(m_ui.hWaveWnd, nullptr, FALSE);
    }
}

void MainWindow::OnPathEditKillFocus(int controlId)
{
    std::wstring path = GetControlText(m_ui.hWnd, controlId);
    if (!path.empty() && PathFileExists(path.c_str())) {
        if (controlId == ID_EDIT_VIDEO) {
            ApplyVideoPath(path);
        } else {
            ApplyAudioPath(path);
        }
    }
}

void MainWindow::OnQualitySelectionChanged()
{
    s_qualityIdx = (int)SendDlgItemMessage(m_ui.hWnd, ID_COMBO_QUALITY, CB_GETCURSEL, 0, 0);
    SetDlgItemText(m_ui.hWnd, ID_STATIC_QINFO, LoadString(PRESETS[s_qualityIdx].dscId).c_str());
}

void MainWindow::OnAudioShortModeChanged()
{
    s_audioShortMode = (AudioShortMode)SendDlgItemMessage(m_ui.hWnd, ID_COMBO_AUDSHORT, CB_GETCURSEL, 0, 0);
}

// ===========================================================================
// Message Handlers: Custom Messages
// ===========================================================================

void MainWindow::OnEncodeProgress(int pct, double etaSecs)
{
    wchar_t buf[256] = {};
    swprintf_s(buf, LoadString(IDS_ENCODING).c_str(), pct, StringUtils::SecondsToHMS(etaSecs).c_str());
    SendDlgItemMessage(m_ui.hWnd, ID_PROGRESS, PBM_SETPOS, (WPARAM)pct, 0);
    SetDlgItemText(m_ui.hWnd, ID_STATIC_STATUS, buf);
    UpdateTaskbarProgress(pct);
}

void MainWindow::OnEncodeDone(ENCODE_DONE_MSG* encMsg)
{
    m_encoding = false;
    EnableWindow(GetDlgItem(m_ui.hWnd, ID_BTN_GO), TRUE);
    ShowWindow(GetDlgItem(m_ui.hWnd, ID_PROGRESS), SW_HIDE);
    
    if (encMsg->ok) {
        UpdateTaskbarProgress(100);
        SetDlgItemText(m_ui.hWnd, ID_STATIC_STATUS, LoadString(IDS_DONE_STATUS).c_str());
        Win32Utils::MessageBox(m_ui.hWnd, LoadString(IDS_DONE_MSG), 
            LoadString(IDS_DONE_TITLE).c_str(), MB_ICONINFORMATION);
    } else {
        SignalTaskbarError();
        SetDlgItemText(m_ui.hWnd, ID_STATIC_STATUS, LoadString(IDS_ERR_STATUS).c_str());
        Win32Utils::MessageBox(m_ui.hWnd, (LoadString(IDS_ERR_TITLE) + L":\n\n" + encMsg->error),
            LoadString(IDS_ERR_TITLE).c_str(), MB_ICONERROR);
    }
    SignalTaskbarDone();
}

void MainWindow::OnWaveformReady(uint64_t generation, WaveformResult* result)
{
    // Ignore results from a request that's been superseded
    if (generation != m_waveformJob.CurrentGeneration()) {
        delete result;
        return;
    }

    if (result) {
        m_ui.waveform = std::move(result->values);
        delete result;
    }
    m_ui.waveformReady = true;
    InvalidateRect(m_ui.hWaveWnd, nullptr, FALSE);
}

// ===========================================================================
// Message Handlers: Miscellaneous
// ===========================================================================

void MainWindow::OnDropFiles(HDROP hDrop)
{
    UINT n = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; i++) {
        const UINT cch = DragQueryFile(hDrop, i, nullptr, 0);
        std::vector<wchar_t> buf(cch + 1, L'\0');
        DragQueryFile(hDrop, i, buf.data(), cch + 1);
        std::wstring path = buf.data();
        
        if (FileUtils::IsVideoFile(path)) {
            ApplyVideoPath(path);
        } else if (FileUtils::IsAudioFile(path)) {
            ApplyAudioPath(path);
        }
    }
    DragFinish(hDrop);
}

void MainWindow::OnHScroll(HWND hSlider)
{
    if (hSlider && (HWND)hSlider == GetDlgItem(m_ui.hWnd, ID_SLIDER_VOLUME)) {
        s_volumePct = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
        wchar_t buf[16];
        swprintf_s(buf, L"%d %%", s_volumePct);
        SetDlgItemText(m_ui.hWnd, ID_STATIC_VOL, buf);
    }
}

LRESULT MainWindow::OnControlColor(HDC hdc)
{
    SetBkColor(hdc, kColorBg);
    SetTextColor(hdc, kColorText);
    return (LRESULT)m_ui.hBrushBg;
}

LRESULT MainWindow::OnEraseBackground(HDC hdc)
{
    RECT rc;
    GetClientRect(m_ui.hWnd, &rc);
    FillRect(hdc, &rc, m_ui.hBrushBg);
    return 1;
}

// ===========================================================================
// Control Creation Utilities
// ===========================================================================

HWND MainWindow::CreateLabel(const wchar_t* text, int x, int y, int w, int h, bool bold)
{
    HWND h2 = CreateWindow(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_ui.hWnd, nullptr, m_hInstance, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(bold ? m_ui.hFontBold : m_ui.hFontUI), TRUE);
    return h2;
}

HWND MainWindow::CreateEdit(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND h2 = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        x, y, w, h, m_ui.hWnd, (HMENU)(INT_PTR)id, m_hInstance, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)m_ui.hFontUI, TRUE);
    return h2;
}

HWND MainWindow::CreateButton(int id, const wchar_t* text, int x, int y, int w, int h, bool isDefault)
{
    HWND h2 = CreateWindow(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | (isDefault ? BS_DEFPUSHBUTTON : 0),
        x, y, w, h, m_ui.hWnd, (HMENU)(INT_PTR)id, m_hInstance, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(isDefault ? m_ui.hFontBold : m_ui.hFontUI), TRUE);
    return h2;
}

HWND MainWindow::CreateStatic(int id, const wchar_t* text, int x, int y, int w, int h, HFONT hFont)
{
    HWND h2 = CreateWindow(L"STATIC", text, WS_CHILD | WS_VISIBLE,
        x, y, w, h, m_ui.hWnd, (HMENU)(INT_PTR)id, m_hInstance, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)(hFont ? hFont : m_ui.hFontUI), TRUE);
    return h2;
}

HWND MainWindow::CreateComboBox(int id, int x, int y, int w, int h)
{
    HWND h2 = CreateWindow(WC_COMBOBOX, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, m_ui.hWnd, (HMENU)(INT_PTR)id, m_hInstance, nullptr);
    SendMessage(h2, WM_SETFONT, (WPARAM)m_ui.hFontUI, TRUE);
    return h2;
}

// ===========================================================================
// File Management
// ===========================================================================

std::wstring MainWindow::BrowseFile(bool isVideo)
{
    if (isVideo) {
        return FileUtils::BrowseVideoFile(m_ui.hWnd);
    } else {
        return FileUtils::BrowseAudioFile(m_ui.hWnd);
    }
}

std::wstring MainWindow::BrowseSave(const std::wstring& defaultName)
{
    return FileUtils::SaveFileDialog(m_ui.hWnd, defaultName, L"MP4\0*.mp4\0All\0*.*\0", L"mp4");
}

void MainWindow::ApplyVideoPath(const std::wstring& path)
{
    if (path.empty() || !FileUtils::FileExists(path)) return;
    
    m_ui.videoPath = path;
    double dur = MediaUtils::GetMediaDuration(path);
    m_ui.videoDuration = dur;
    SetDlgItemText(m_ui.hWnd, ID_EDIT_VIDEO, path.c_str());
    
    if (dur > 0) {
        SetDlgItemText(m_ui.hWnd, ID_EDIT_VID_END, StringUtils::SecondsToHMS(dur).c_str());
        SetDlgItemText(m_ui.hWnd, ID_STATIC_VID_DUR,
            FormatString2(IDS_DURATION_FMT, StringUtils::SecondsToHMS(dur).c_str(), 
                          StringUtils::GetFileName(path).c_str()).c_str());
    } else {
        SetDlgItemText(m_ui.hWnd, ID_STATIC_VID_DUR, LoadString(IDS_DUR_UNAVAIL).c_str());
    }
}

void MainWindow::ApplyAudioPath(const std::wstring& path)
{
    if (path.empty() || !FileUtils::FileExists(path)) return;
    
    m_ui.audioPath = path;
    m_ui.audioStartSec = 0;
    m_ui.waveformReady = false;
    m_ui.waveform.clear();
    double dur = MediaUtils::GetMediaDuration(path);
    m_ui.audioDuration = dur;
    m_ui.audioEndSec = (dur > 0) ? dur : 0;
    
    SetDlgItemText(m_ui.hWnd, ID_EDIT_AUDIO, path.c_str());
    SetDlgItemText(m_ui.hWnd, ID_EDIT_AUD_START, L"00:00:00");
    SetDlgItemText(m_ui.hWnd, ID_EDIT_AUD_END, (dur > 0) ? StringUtils::SecondsToHMS(dur).c_str() : L"");
    
    if (dur > 0) {
        SetDlgItemText(m_ui.hWnd, ID_STATIC_AUD_DUR,
            FormatString2(IDS_DURATION_FMT, StringUtils::SecondsToHMS(dur).c_str(), 
                          StringUtils::GetFileName(path).c_str()).c_str());
    }
    InvalidateRect(m_ui.hWaveWnd, nullptr, FALSE);

    m_waveformJob.Start(path, dur, [this](uint64_t generation, std::vector<float> values) {
        auto* result = values.empty() ? nullptr : new WaveformResult{ generation, std::move(values) };
        if (!PostMessage(m_ui.hWnd, WM_WAVEFORM_READY, (WPARAM)generation, (LPARAM)result)) {
            delete result;
        }
    });
}

// ===========================================================================
// Utility Functions
// ===========================================================================

// Utility functions - now using StringUtils
std::wstring MainWindow::SecondsToHMS(double s)
{
    return StringUtils::SecondsToHMS(s);
}

double MainWindow::HMSToSeconds(const std::wstring& t)
{
    return StringUtils::HMSToSeconds(t);
}

std::wstring MainWindow::GetControlText(HWND hWnd, int id)
{
    HWND h = GetDlgItem(hWnd, id);
    int n = GetWindowTextLength(h) + 1;
    std::wstring s(n, L'\0');
    GetWindowText(h, s.data(), n);
    s.resize(wcslen(s.c_str()));
    return s;
}

void MainWindow::ShowError(UINT msgId, UINT titleId)
{
    Win32Utils::MessageBox(m_ui.hWnd, LoadString(msgId), LoadString(titleId), MB_ICONWARNING);
}

std::wstring MainWindow::LocalizeEncodeError(EncodeError err)
{
    switch (err) {
        case EncodeError::VideoOpenFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_OPEN_FAILED);
        case EncodeError::VideoStreamSelectFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_STREAM_SELECT_FAILED);
        case EncodeError::VideoDecodeFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_DECODE_FAILED);
        case EncodeError::VideoTypeReadFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_TYPE_READ_FAILED);
        case EncodeError::VideoDimensionsInvalid:
            return LoadString(IDS_ENC_ERR_VIDEO_DIMENSIONS_INVALID);
        case EncodeError::AudioOpenFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_OPEN_FAILED);
        case EncodeError::AudioStreamSelectFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_STREAM_SELECT_FAILED);
        case EncodeError::AudioFormatFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_FORMAT_FAILED);
        case EncodeError::AudioSeekFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_SEEK_FAILED);
        case EncodeError::AudioTypeReadFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_TYPE_READ_FAILED);
        case EncodeError::OutputCreateFailed:
            return LoadString(IDS_ENC_ERR_OUTPUT_CREATE_FAILED);
        case EncodeError::VideoStreamAddFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_STREAM_ADD_FAILED);
        case EncodeError::VideoStreamTypeIncompatible:
            return LoadString(IDS_ENC_ERR_VIDEO_STREAM_TYPE_INCOMPATIBLE);
        case EncodeError::AudioStreamAddFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_STREAM_ADD_FAILED);
        case EncodeError::AudioStreamConfigFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_STREAM_CONFIG_FAILED);
        case EncodeError::VideoSeekFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_SEEK_FAILED);
        case EncodeError::SinkWriterBeginFailed:
            return LoadString(IDS_ENC_ERR_SINK_WRITER_BEGIN_FAILED);
        case EncodeError::SinkWriterFinalizeFailed:
            return LoadString(IDS_ENC_ERR_SINK_WRITER_FINALIZE_FAILED);
        case EncodeError::VideoReadFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_READ_FAILED);
        case EncodeError::VideoWriteFailed:
            return LoadString(IDS_ENC_ERR_VIDEO_WRITE_FAILED);
        case EncodeError::AudioReadFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_READ_FAILED);
        case EncodeError::AudioWriteFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_WRITE_FAILED);
        case EncodeError::AudioSilenceWriteFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_SILENCE_WRITE_FAILED);
        case EncodeError::AudioLoopRestartFailed:
            return LoadString(IDS_ENC_ERR_AUDIO_LOOP_RESTART_FAILED);
        case EncodeError::Cancelled:
            return LoadString(IDS_ENC_ERR_CANCELLED);
        case EncodeError::None:
        case EncodeError::Unknown:
        default:
            return LoadString(IDS_ENC_ERR_UNKNOWN);
    }
}

// Use FileUtils for path comparison
bool MainWindow::ArePathsEqual(const std::wstring& a, const std::wstring& b)
{
    return FileUtils::ArePathsEqual(a, b);
}

std::wstring MainWindow::LoadString(UINT id)
{
    wchar_t buf[512] = {};
    if (LoadStringW(m_hInstance, id, buf, 512) > 0) {
        return buf;
    }
    return L"";
}

std::wstring MainWindow::FormatString(UINT id, int value)
{
    std::wstring format = LoadString(id);
    wchar_t buf[256] = {};
    swprintf_s(buf, format.c_str(), value);
    return buf;
}

std::wstring MainWindow::FormatString2(UINT id, const wchar_t* a, const wchar_t* b)
{
    std::wstring format = LoadString(id);
    wchar_t buf[512] = {};
    swprintf_s(buf, format.c_str(), a, b);
    return buf;
}

void MainWindow::UpdateTaskbarProgress(int pct)
{
    if (!m_ui.pTaskbar || !m_ui.hWnd) return;
    m_ui.pTaskbar->SetProgressState(m_ui.hWnd, TBPF_NORMAL);
    m_ui.pTaskbar->SetProgressValue(m_ui.hWnd, (ULONGLONG)pct, 100ULL);
}

void MainWindow::SignalTaskbarDone()
{
    if (!m_ui.pTaskbar || !m_ui.hWnd) return;
    m_ui.pTaskbar->SetProgressState(m_ui.hWnd, TBPF_NOPROGRESS);
}

void MainWindow::SignalTaskbarError()
{
    if (!m_ui.pTaskbar || !m_ui.hWnd) return;
    m_ui.pTaskbar->SetProgressState(m_ui.hWnd, TBPF_ERROR);
}

// ===========================================================================
// Waveform Drawing
// ===========================================================================

void MainWindow::DrawWaveform(HWND hWnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom, mid = H / 2, maxAmp = mid - 4;

    HBRUSH hBg = CreateSolidBrush(kColorWaveBg);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    if (m_ui.waveform.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(80, 80, 120));
        HFONT hf = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, hf);
        std::wstring txt = m_ui.audioPath.empty() ? LoadString(IDS_WAVE_LOAD)
            : (m_ui.waveformReady ? LoadString(IDS_WAVE_UNAVAIL) : LoadString(IDS_WAVE_ANALYZING));
        DrawText(hdc, txt.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(hf);
        return;
    }

    double dur = m_ui.audioDuration;
    int cxS = (dur > 0) ? (int)(m_ui.audioStartSec / dur * W) : 0;
    int cxE = (dur > 0) ? (int)(m_ui.audioEndSec / dur * W) : W;

    if (cxS > 0) {
        RECT z = { 0, 0, cxS, H };
        HBRUSH h = CreateSolidBrush(kColorZone);
        FillRect(hdc, &z, h);
        DeleteObject(h);
    }
    if (cxE > cxS) {
        RECT z = { cxS, 0, cxE, H };
        HBRUSH h = CreateSolidBrush(kColorZoneSelected);
        FillRect(hdc, &z, h);
        DeleteObject(h);
    }

    int nb = (int)m_ui.waveform.size();
    for (int i = 0; i < nb; i++) {
        float v = m_ui.waveform[i];
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
    
    DrawCursor(cxS, kColorCursorStart, true);
    DrawCursor(cxE, kColorCursorEnd, false);

    if (dur > 0) {
        std::wstring lbl = L"\u25b6 " + SecondsToHMS(m_ui.audioStartSec)
            + L"  \u2192  " + SecondsToHMS(m_ui.audioEndSec)
            + L" / " + SecondsToHMS(dur);
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
