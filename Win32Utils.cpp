#include "Win32Utils.hpp"

#include <shlwapi.h>
#include <shellapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// ===========================================================================
// RAII Wrappers for Win32 Resources
// ===========================================================================

// Font RAII Wrapper
Win32Utils::Font::Font(HFONT hFont) : m_hFont(hFont) {}

Win32Utils::Font::~Font() {
    if (m_hFont) {
        DeleteObject(m_hFont);
    }
}

Win32Utils::Font::Font(Font&& other) noexcept : m_hFont(other.m_hFont) {
    other.m_hFont = nullptr;
}

Win32Utils::Font& Win32Utils::Font::operator=(Font&& other) noexcept {
    if (this != &other) {
        Reset();
        m_hFont = other.m_hFont;
        other.m_hFont = nullptr;
    }
    return *this;
}

HFONT Win32Utils::Font::Get() const { return m_hFont; }

HFONT Win32Utils::Font::Release() {
    HFONT tmp = m_hFont;
    m_hFont = nullptr;
    return tmp;
}

void Win32Utils::Font::Reset(HFONT hFont) {
    if (m_hFont && m_hFont != hFont) {
        DeleteObject(m_hFont);
    }
    m_hFont = hFont;
}

Win32Utils::Font::operator HFONT() const { return m_hFont; }

// Brush RAII Wrapper
Win32Utils::Brush::Brush(HBRUSH hBrush) : m_hBrush(hBrush) {}

Win32Utils::Brush::~Brush() {
    if (m_hBrush) {
        DeleteObject(m_hBrush);
    }
}

Win32Utils::Brush::Brush(Brush&& other) noexcept : m_hBrush(other.m_hBrush) {
    other.m_hBrush = nullptr;
}

Win32Utils::Brush& Win32Utils::Brush::operator=(Brush&& other) noexcept {
    if (this != &other) {
        Reset();
        m_hBrush = other.m_hBrush;
        other.m_hBrush = nullptr;
    }
    return *this;
}

HBRUSH Win32Utils::Brush::Get() const { return m_hBrush; }

HBRUSH Win32Utils::Brush::Release() {
    HBRUSH tmp = m_hBrush;
    m_hBrush = nullptr;
    return tmp;
}

void Win32Utils::Brush::Reset(HBRUSH hBrush) {
    if (m_hBrush && m_hBrush != hBrush) {
        DeleteObject(m_hBrush);
    }
    m_hBrush = hBrush;
}

Win32Utils::Brush::operator HBRUSH() const { return m_hBrush; }

// Pen RAII Wrapper
Win32Utils::Pen::Pen(HPEN hPen) : m_hPen(hPen) {}

Win32Utils::Pen::~Pen() {
    if (m_hPen) {
        DeleteObject(m_hPen);
    }
}

Win32Utils::Pen::Pen(Pen&& other) noexcept : m_hPen(other.m_hPen) {
    other.m_hPen = nullptr;
}

Win32Utils::Pen& Win32Utils::Pen::operator=(Pen&& other) noexcept {
    if (this != &other) {
        Reset();
        m_hPen = other.m_hPen;
        other.m_hPen = nullptr;
    }
    return *this;
}

HPEN Win32Utils::Pen::Get() const { return m_hPen; }

HPEN Win32Utils::Pen::Release() {
    HPEN tmp = m_hPen;
    m_hPen = nullptr;
    return tmp;
}

void Win32Utils::Pen::Reset(HPEN hPen) {
    if (m_hPen && m_hPen != hPen) {
        DeleteObject(m_hPen);
    }
    m_hPen = hPen;
}

Win32Utils::Pen::operator HPEN() const { return m_hPen; }

// Bitmap RAII Wrapper
Win32Utils::Bitmap::Bitmap(HBITMAP hBitmap) : m_hBitmap(hBitmap) {}

Win32Utils::Bitmap::~Bitmap() {
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
    }
}

Win32Utils::Bitmap::Bitmap(Bitmap&& other) noexcept : m_hBitmap(other.m_hBitmap) {
    other.m_hBitmap = nullptr;
}

Win32Utils::Bitmap& Win32Utils::Bitmap::operator=(Bitmap&& other) noexcept {
    if (this != &other) {
        Reset();
        m_hBitmap = other.m_hBitmap;
        other.m_hBitmap = nullptr;
    }
    return *this;
}

HBITMAP Win32Utils::Bitmap::Get() const { return m_hBitmap; }

HBITMAP Win32Utils::Bitmap::Release() {
    HBITMAP tmp = m_hBitmap;
    m_hBitmap = nullptr;
    return tmp;
}

void Win32Utils::Bitmap::Reset(HBITMAP hBitmap) {
    if (m_hBitmap && m_hBitmap != hBitmap) {
        DeleteObject(m_hBitmap);
    }
    m_hBitmap = hBitmap;
}

Win32Utils::Bitmap::operator HBITMAP() const { return m_hBitmap; }

// DC RAII Wrapper
Win32Utils::DC::DC(HDC hDC, bool manage) : m_hDC(hDC), m_manage(manage) {}

Win32Utils::DC::~DC() {
    if (m_hDC && m_manage) {
        DeleteDC(m_hDC);
    }
}

Win32Utils::DC::DC(DC&& other) noexcept : m_hDC(other.m_hDC), m_manage(other.m_manage) {
    other.m_hDC = nullptr;
    other.m_manage = false;
}

Win32Utils::DC& Win32Utils::DC::operator=(DC&& other) noexcept {
    if (this != &other) {
        Reset();
        m_hDC = other.m_hDC;
        m_manage = other.m_manage;
        other.m_hDC = nullptr;
        other.m_manage = false;
    }
    return *this;
}

HDC Win32Utils::DC::Get() const { return m_hDC; }

HDC Win32Utils::DC::Release() {
    HDC tmp = m_hDC;
    m_hDC = nullptr;
    m_manage = false;
    return tmp;
}

void Win32Utils::DC::Reset(HDC hDC) {
    if (m_hDC && m_hDC != hDC && m_manage) {
        DeleteDC(m_hDC);
    }
    m_hDC = hDC;
}

Win32Utils::DC::operator HDC() const { return m_hDC; }

// ===========================================================================
// Taskbar Utilities
// ===========================================================================

ITaskbarList3* Win32Utils::InitializeTaskbarInterface()
{
    ITaskbarList3* pTaskbar = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, 
                                 IID_PPV_ARGS(&pTaskbar));
    
    if (SUCCEEDED(hr) && pTaskbar) {
        pTaskbar->HrInit();
    }
    
    return pTaskbar;
}

void Win32Utils::ReleaseTaskbarInterface(ITaskbarList3* pTaskbar)
{
    if (pTaskbar) {
        pTaskbar->Release();
    }
}

void Win32Utils::SetTaskbarProgress(HWND hWnd, ITaskbarList3* pTaskbar, ULONGLONG current, ULONGLONG total)
{
    if (!pTaskbar || !hWnd) return;
    
    if (total > 0) {
        pTaskbar->SetProgressState(hWnd, TBPF_NORMAL);
        pTaskbar->SetProgressValue(hWnd, current, total);
    } else {
        pTaskbar->SetProgressState(hWnd, TBPF_INDETERMINATE);
    }
}

void Win32Utils::SetTaskbarState(HWND hWnd, ITaskbarList3* pTaskbar, TBPFLAG state)
{
    if (!pTaskbar || !hWnd) return;
    pTaskbar->SetProgressState(hWnd, state);
}

// ===========================================================================
// Window Utilities
// ===========================================================================

void Win32Utils::CenterWindow(HWND hWnd, HWND hParent)
{
    if (!hWnd) return;
    
    RECT windowRect;
    GetWindowRect(hWnd, &windowRect);
    
    if (hParent) {
        RECT parentRect;
        GetWindowRect(hParent, &parentRect);
        
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;
        int parentWidth = parentRect.right - parentRect.left;
        int parentHeight = parentRect.bottom - parentRect.top;
        
        int x = parentRect.left + (parentWidth - width) / 2;
        int y = parentRect.top + (parentHeight - height) / 2;
        
        SetWindowPos(hWnd, nullptr, x, y, width, height, SWP_NOSIZE | SWP_NOZORDER);
    } else {
        RECT workArea = GetWorkArea();
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;
        
        int x = workArea.left + (workArea.right - workArea.left - width) / 2;
        int y = workArea.top + (workArea.bottom - workArea.top - height) / 2;
        
        SetWindowPos(hWnd, nullptr, x, y, width, height, SWP_NOSIZE | SWP_NOZORDER);
    }
}

RECT Win32Utils::GetWorkArea()
{
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    return workArea;
}

UINT Win32Utils::GetDPI(HWND hWnd)
{
    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    if (!hUser32) return 96; // Default DPI
    
    typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
    GetDpiForWindowProc GetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
    
    if (GetDpiForWindow) {
        return GetDpiForWindow(hWnd);
    }
    
    // Fallback for older Windows versions
    HMODULE hShcore = LoadLibrary(L"shcore.dll");
    if (hShcore) {
        typedef HRESULT(WINAPI* GetDpiForMonitorProc)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT%);
        GetDpiForMonitorProc GetDpiForMonitor = (GetDpiForMonitorProc)GetProcAddress(hShcore, "GetDpiForMonitor");
        
        if (GetDpiForMonitor) {
            HMONITOR hMonitor = hWnd ? MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST) : GetDC(nullptr);
            UINT dpiX = 96, dpiY = 96;
            
            if (hMonitor) {
                GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            }
            
            FreeLibrary(hShcore);
            return dpiX;
        }
        
        FreeLibrary(hShcore);
    }
    
    return 96; // Default DPI
}

int Win32Utils::PixelsToPoints(int pixels, HWND hWnd)
{
    UINT dpi = GetDPI(hWnd);
    if (dpi == 0) dpi = 96; // Fallback to default DPI
    
    return static_cast<int>(pixels * 96.0 / static_cast<double>(dpi));
}

// ===========================================================================
// GDI Helper Functions
// ===========================================================================

HFONT Win32Utils::CreateFont(int height, int weight, bool italic, bool underline, const wchar_t* faceName)
{
    return ::CreateFontW(
        height,                        // Height
        0,                            // Width
        0,                            // Escapement
        0,                            // Orientation
        weight,                       // Weight
        italic ? TRUE : FALSE,        // Italic
        underline ? TRUE : FALSE,     // Underline
        0,                            // StrikeOut
        DEFAULT_CHARSET,              // CharSet
        OUT_DEFAULT_PRECIS,           // OutputPrecision
        CLIP_DEFAULT_PRECIS,          // ClipPrecision
        CLEARTYPE_QUALITY,           // Quality
        DEFAULT_PITCH | FF_DONTCARE,   // PitchAndFamily
        faceName                      // FaceName
    );
}

HBRUSH Win32Utils::CreateSolidBrush(COLORREF color)
{
    return ::CreateSolidBrush(color);
}

HPEN Win32Utils::CreatePen(int style, int width, COLORREF color)
{
    return ::CreatePen(style, width, color);
}

// ===========================================================================
// Color Utilities
// ===========================================================================

COLORREF Win32Utils::RGB(byte r, byte g, byte b)
{
    return RGB(r, g, b);
}

COLORREF Win32Utils::DarkenColor(COLORREF color, float factor)
{
    if (factor <= 0.0f) return color;
    if (factor >= 1.0f) return RGB(0, 0, 0);
    
    byte r = GetRValue(color);
    byte g = GetGValue(color);
    byte b = GetBValue(color);
    
    r = static_cast<byte>(r * (1.0f - factor));
    g = static_cast<byte>(g * (1.0f - factor));
    b = static_cast<byte>(b * (1.0f - factor));
    
    return RGB(r, g, b);
}

COLORREF Win32Utils::LightenColor(COLORREF color, float factor)
{
    if (factor <= 0.0f) return color;
    if (factor >= 1.0f) return RGB(255, 255, 255);
    
    byte r = GetRValue(color);
    byte g = GetGValue(color);
    byte b = GetBValue(color);
    
    int remaining = static_cast<int>(factor * 255);
    r = static_cast<byte>(std::min(255, r + remaining));
    g = static_cast<byte>(std::min(255, g + remaining));
    b = static_cast<byte>(std::min(255, b + remaining));
    
    return RGB(r, g, b);
}

COLORREF Win32Utils::BlendColors(COLORREF color1, COLORREF color2, float ratio)
{
    if (ratio <= 0.0f) return color1;
    if (ratio >= 1.0f) return color2;
    
    byte r1 = GetRValue(color1);
    byte g1 = GetGValue(color1);
    byte b1 = GetBValue(color1);
    
    byte r2 = GetRValue(color2);
    byte g2 = GetGValue(color2);
    byte b2 = GetBValue(color2);
    
    byte r = static_cast<byte>(r1 + (r2 - r1) * ratio);
    byte g = static_cast<byte>(g1 + (g2 - g1) * ratio);
    byte b = static_cast<byte>(b1 + (b2 - b1) * ratio);
    
    return RGB(r, g, b);
}

// ===========================================================================
// String and Localization
// ===========================================================================

std::wstring Win32Utils::LoadString(HINSTANCE hInstance, UINT id)
{
    wchar_t buffer[512] = {};
    if (LoadStringW(hInstance, id, buffer, 512) > 0) {
        return buffer;
    }
    return L"";
}

int Win32Utils::MessageBox(HWND hWnd, const std::wstring& text, const std::wstring& caption, UINT type)
{
    return ::MessageBoxW(hWnd, text.c_str(), caption.c_str(), type);
}

// ===========================================================================
// System Information
// ===========================================================================

bool Win32Utils::IsWindows10OrGreater()
{
    return GetOSVersion() >= 10000; // Windows 10 version 1507 build 10000+
}

bool Win32Utils::IsWindows11OrGreater()
{
    return GetOSVersion() >= 22000; // Windows 11 version 21H2 build 22000+
}

DWORD Win32Utils::GetOSVersion()
{
    static DWORD version = 0;
    if (version != 0) return version;
    
    // Try to get the real version using GetVersionEx
    OSVERSIONINFOEX osvi = { sizeof(osvi) };
    NTSTATUS(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEX) = nullptr;
    
    HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
    if (hNtdll) {
        *(FARPROC*)&RtlGetVersion = GetProcAddress(hNtdll, "RtlGetVersion");
    }
    
    if (RtlGetVersion) {
        if (RtlGetVersion(&osvi) == 0) {
            version = (osvi.dwMajorVersion << 16) | (osvi.dwMinorVersion << 8) | osvi.dwBuildNumber;
            return version;
        }
    }
    
    // Fallback to GetVersionEx
    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        version = (osvi.dwMajorVersion << 16) | (osvi.dwMinorVersion << 8) | osvi.dwBuildNumber;
    } else {
        version = 0x000A0000; // Windows 10 as fallback
    }
    
    return version;
}
