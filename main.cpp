// ===========================================================================
//  Video Music Replacer / Remplaceur de Musique Video
//  Win32 + Media Foundation -- MSVC 2019/2022 -- C++17 x64
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include "MainWindow.hpp"

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
// WinMain - Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    // Create and run the main application window
    MainWindow mainWindow(hInstance);
    
    if (!mainWindow.Create(nCmdShow)) {
        return 1;
    }
    
    mainWindow.RunMessageLoop();
    
    return 0;
}