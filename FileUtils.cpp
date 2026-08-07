#include "FileUtils.hpp"
#include "StringUtils.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <vector>
#include <algorithm>

// For shortcut creation
#include <propkey.h>

// ===========================================================================
// File Dialog Functions
// ===========================================================================

std::wstring FileUtils::OpenFileDialog(void* hOwner, const std::wstring& title, 
                                      const std::wstring& filter, const std::wstring& defaultExtension)
{
    constexpr DWORD kDialogPathChars = 32768;
    std::vector<wchar_t> buf(kDialogPathChars, L'\0');
    
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(hOwner);
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = kDialogPathChars;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    
    if (!defaultExtension.empty()) {
        ofn.lpstrDefExt = defaultExtension.c_str();
    }
    
    return GetOpenFileName(&ofn) ? buf.data() : L"";
}

std::wstring FileUtils::BrowseVideoFile(void* hOwner)
{
    return OpenFileDialog(
        hOwner,
        L"Video",
        L"Video\0*.mp4;*.mov;*.avi;*.mkv;*.m4v;*.wmv\0All\0*.*\0"
    );
}

std::wstring FileUtils::BrowseAudioFile(void* hOwner)
{
    return OpenFileDialog(
        hOwner,
        L"Music",
        L"Audio\0*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a;*.wma\0All\0*.*\0"
    );
}

std::wstring FileUtils::SaveFileDialog(void* hOwner, const std::wstring& defaultName,
                                      const std::wstring& filter, const std::wstring& defaultExtension)
{
    constexpr DWORD kDialogPathChars = 32768;
    std::vector<wchar_t> buf(kDialogPathChars, L'\0');
    
    if (!defaultName.empty()) {
        wcsncpy_s(buf.data(), buf.size(), defaultName.c_str(), _TRUNCATE);
    }
    
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(hOwner);
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = kDialogPathChars;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrDefExt = defaultExtension.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    
    return GetSaveFileName(&ofn) ? buf.data() : L"";
}

// ===========================================================================
// File Information Functions
// ===========================================================================

bool FileUtils::FileExists(const std::wstring& filePath)
{
    if (filePath.empty()) return false;
    return PathFileExists(filePath.c_str()) != FALSE;
}

bool FileUtils::DirectoryExists(const std::wstring& directoryPath)
{
    if (directoryPath.empty()) return false;
    
    DWORD attrib = GetFileAttributes(directoryPath.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES) && (attrib & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t FileUtils::GetFileSize(const std::wstring& filePath)
{
    if (!FileExists(filePath)) return 0;
    
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesEx(filePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return 0;
    }
    
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = fileInfo.nFileSizeLow;
    fileSize.HighPart = fileInfo.nFileSizeHigh;
    
    return fileSize.QuadPart;
}

void FileUtils::GetFileLastWriteTime(const std::wstring& filePath, void* lastWriteTime)
{
    if (!FileExists(filePath) || !lastWriteTime) return;
    
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesEx(filePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        *static_cast<FILETIME*>(lastWriteTime) = fileInfo.ftLastWriteTime;
    }
}

// ===========================================================================
// Path Manipulation Functions
// ===========================================================================

bool FileUtils::ArePathsEqual(const std::wstring& path1, const std::wstring& path2)
{
    if (path1.empty() || path2.empty()) return false;
    
    wchar_t fullPath1[32768] = {};
    wchar_t fullPath2[32768] = {};
    
    DWORD len1 = GetFullPathNameW(path1.c_str(), (DWORD)std::size(fullPath1), fullPath1, nullptr);
    DWORD len2 = GetFullPathNameW(path2.c_str(), (DWORD)std::size(fullPath2), fullPath2, nullptr);
    
    if (len1 == 0 || len1 >= std::size(fullPath1) || len2 == 0 || len2 >= std::size(fullPath2)) {
        return false;
    }
    
    return CompareStringOrdinal(fullPath1, -1, fullPath2, -1, TRUE) == CSTR_EQUAL;
}

std::wstring FileUtils::GetAbsolutePath(const std::wstring& relativePath)
{
    if (relativePath.empty()) return L"";
    
    wchar_t fullPath[32768] = {};
    DWORD len = GetFullPathNameW(relativePath.c_str(), (DWORD)std::size(fullPath), fullPath, nullptr);
    
    if (len == 0 || len >= std::size(fullPath)) {
        return L"";
    }
    
    return fullPath;
}

std::wstring FileUtils::GetParentDirectory(const std::wstring& path)
{
    if (path.empty()) return L"";
    
    std::wstring normalized = NormalizePath(path);
    size_t lastSlash = normalized.find_last_of(L"\\");
    
    if (lastSlash == std::wstring::npos) {
        // Check if it's a drive root (like C:\")
        if (normalized.length() >= 2 && normalized[1] == L':') {
            return normalized.substr(0, 2);
        }
        return L"";
    }
    
    // Handle case where the slash is at position 2 (C:\")
    if (lastSlash == 2 && normalized.length() > 3 && normalized[1] == L':') {
        return normalized.substr(0, 3); // Return C:\
    }
    
    if (lastSlash == 0) {
        return L"\\";
    }
    
    return normalized.substr(0, lastSlash);
}

std::wstring FileUtils::CombinePaths(const std::vector<std::wstring>& parts)
{
    if (parts.empty()) return L"";
    if (parts.size() == 1) return parts[0];
    
    std::wstring result;
    
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::wstring& part = parts[i];
        
        if (part.empty()) continue;
        
        if (result.empty()) {
            result = part;
        } else {
            // Check if the current part starts with a separator
            if (part[0] == L'\\' || part[0] == L'/') {
                result += part;
            } else {
                // Check if the result ends with a separator
                if (result.back() == L'\\' || result.back() == L'/') {
                    result += part;
                } else {
                    result += L"\\" + part;
                }
            }
        }
    }
    
    return NormalizePath(result);
}

std::wstring FileUtils::NormalizePath(const std::wstring& path)
{
    if (path.empty()) return L"";
    
    std::wstring result = path;
    
    // Replace all forward slashes with backslashes
    std::replace(result.begin(), result.end(), L'/', L'\\');
    
    // Remove trailing backslash (except for root paths like C:\")
    while (!result.empty() && result.back() == L'\\') {
        result.pop_back();
    }
    
    // Handle special cases
    if (result.length() == 2 && result[1] == L':') {
        result += L'\\';
    }
    
    return result;
}

// ===========================================================================
// File Type Functions
// ===========================================================================

bool FileUtils::IsVideoFile(const std::wstring& filePath)
{
    if (filePath.empty()) return false;
    
    std::wstring ext = StringUtils::GetFileExtension(filePath);
    ext = StringUtils::ToLower(ext);
    
    static const wchar_t* videoExtensions[] = {
        L"mp4", L"mov", L"avi", L"mkv", L"m4v", L"wmv", 
        L"flv", L"webm", L"mpeg", L"mpg", L"3gp", L"3g2"
    };
    
    for (const wchar_t* extName : videoExtensions) {
        if (ext == extName) {
            return true;
        }
    }
    
    return false;
}

bool FileUtils::IsAudioFile(const std::wstring& filePath)
{
    if (filePath.empty()) return false;
    
    std::wstring ext = StringUtils::GetFileExtension(filePath);
    ext = StringUtils::ToLower(ext);
    
    static const wchar_t* audioExtensions[] = {
        L"mp3", L"wav", L"aac", L"flac", L"ogg", L"m4a", 
        L"wma", L"alac", L"aiff", L"au", L"caf"
    };
    
    for (const wchar_t* extName : audioExtensions) {
        if (ext == extName) {
            return true;
        }
    }
    
    return false;
}

bool FileUtils::IsImageFile(const std::wstring& filePath)
{
    if (filePath.empty()) return false;
    
    std::wstring ext = StringUtils::GetFileExtension(filePath);
    ext = StringUtils::ToLower(ext);
    
    static const wchar_t* imageExtensions[] = {
        L"jpg", L"jpeg", L"png", L"gif", L"bmp", L"tiff",
        L"webp", L"ico", L"svg", L"psd"
    };
    
    for (const wchar_t* extName : imageExtensions) {
        if (ext == extName) {
            return true;
        }
    }
    
    return false;
}

std::wstring FileUtils::GetMimeType(const std::wstring& filePath)
{
    if (filePath.empty()) return L"";
    
    std::wstring ext = StringUtils::GetFileExtension(filePath);
    ext = StringUtils::ToLower(ext);
    
    // Video MIME types
    if (ext == L"mp4") return L"video/mp4";
    if (ext == L"mov") return L"video/quicktime";
    if (ext == L"avi") return L"video/x-msvideo";
    if (ext == L"mkv") return L"video/x-matroska";
    if (ext == L"webm") return L"video/webm";
    if (ext == L"wmv") return L"video/x-ms-wmv";
    if (ext == L"flv") return L"video/x-flv";
    
    // Audio MIME types
    if (ext == L"mp3") return L"audio/mpeg";
    if (ext == L"wav") return L"audio/wav";
    if (ext == L"aac") return L"audio/aac";
    if (ext == L"flac") return L"audio/flac";
    if (ext == L"ogg") return L"audio/ogg";
    if (ext == L"m4a") return L"audio/mp4";
    if (ext == L"wma") return L"audio/x-ms-wma";
    
    // Image MIME types
    if (ext == L"jpg" || ext == L"jpeg") return L"image/jpeg";
    if (ext == L"png") return L"image/png";
    if (ext == L"gif") return L"image/gif";
    if (ext == L"bmp") return L"image/bmp";
    if (ext == L"tiff") return L"image/tiff";
    if (ext == L"webp") return L"image/webp";
    if (ext == L"svg") return L"image/svg+xml";
    
    return L"application/octet-stream";
}

// ===========================================================================
// File Operations
// ===========================================================================

bool FileUtils::CopyFile(const std::wstring& source, const std::wstring& destination, bool overwrite)
{
    if (!FileExists(source)) return false;
    
    return ::CopyFileW(source.c_str(), destination.c_str(), overwrite ? FALSE : TRUE) != FALSE;
}

bool FileUtils::MoveFile(const std::wstring& source, const std::wstring& destination)
{
    if (!FileExists(source)) return false;
    
    return ::MoveFileW(source.c_str(), destination.c_str()) != FALSE;
}

bool FileUtils::DeleteFile(const std::wstring& filePath)
{
    if (!FileExists(filePath)) return false;
    
    return ::DeleteFileW(filePath.c_str()) != FALSE;
}

bool FileUtils::CreateDirectory(const std::wstring& directoryPath)
{
    if (DirectoryExists(directoryPath)) return true;
    
    // Create all parent directories
    std::wstring path = NormalizePath(directoryPath);
    
    if (path.length() >= 2 && path[1] == L':') {
        // Absolute path with drive letter
        if (!DirectoryExists(path.substr(0, 3))) {
            return false; // Drive doesn't exist
        }
        path = path.substr(3);
    }
    
    std::wstring currentPath;
    
    // Skip the first separator if present
    size_t start = 0;
    if (!path.empty() && path[0] == L'\\') {
        start = 1;
    }
    
    while (start < path.length()) {
        size_t nextSlash = path.find(L'\\', start);
        if (nextSlash == std::wstring::npos) {
            nextSlash = path.length();
        }
        
        std::wstring dirPart = path.substr(0, nextSlash);
        if (!currentPath.empty()) {
            currentPath += L"\\" + dirPart;
        } else {
            currentPath = dirPart;
        }
        
        if (!DirectoryExists(currentPath)) {
            if (!::CreateDirectoryW(currentPath.c_str(), nullptr)) {
                return false;
            }
        }
        
        start = nextSlash + 1;
    }
    
    return true;
}

// ===========================================================================
// Temporary Files
// ===========================================================================

std::wstring FileUtils::CreateTempFile(const std::wstring& extension)
{
    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFileName[MAX_PATH] = {};
    
    // Get temp directory
    DWORD result = GetTempPathW(MAX_PATH, tempPath);
    if (result == 0 || result >= MAX_PATH) {
        return L"";
    }
    
    // Generate temp file name
    if (extension.empty()) {
        result = GetTempFileNameW(tempPath, L"tmp", 0, tempFileName);
    } else {
        // Create a temp file name with the specified extension
        std::wstring prefix = L"tmp";
        std::wstring ext = extension;
        if (!ext.empty() && ext[0] != L'.') {
            ext = L"." + ext;
        }
        result = GetTempFileNameW(tempPath, prefix.c_str(), static_cast<UINT>(ext.length() + 1), tempFileName);
    }
    
    if (result == 0 || result >= MAX_PATH) {
        return L"";
    }
    
    return tempFileName;
}

std::wstring FileUtils::GetTempDirectory()
{
    wchar_t tempPath[MAX_PATH] = {};
    DWORD result = GetTempPathW(MAX_PATH, tempPath);
    
    if (result == 0 || result >= MAX_PATH) {
        return L"";
    }
    
    return tempPath;
}

// ===========================================================================
// Shortcut Functions
// ===========================================================================

bool FileUtils::CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetPath, 
                               const std::wstring& description)
{
    if (shortcutPath.empty() || targetPath.empty()) return false;
    
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) return false;
    
    bool success = false;
    
    try {
        IShellLinkW* pShellLink = nullptr;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pShellLink);
        
        if (SUCCEEDED(hr)) {
            pShellLink->SetPath(targetPath.c_str());
            
            if (!description.empty()) {
                pShellLink->SetDescription(description.c_str());
            }
            
            IPersistFile* pPersistFile = nullptr;
            hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
            
            if (SUCCEEDED(hr)) {
                hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
                pPersistFile->Release();
                success = SUCCEEDED(hr);
            }
            
            pShellLink->Release();
        }
    } catch (...) {
        // Exception occurred
    }
    
    CoUninitialize();
    return success;
}

std::wstring FileUtils::GetShortcutTarget(const std::wstring& shortcutPath)
{
    if (shortcutPath.empty() || !FileExists(shortcutPath)) return L"";
    
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) return L"";
    
    std::wstring targetPath;
    
    try {
        IShellLinkW* pShellLink = nullptr;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pShellLink);
        
        if (SUCCEEDED(hr)) {
            IPersistFile* pPersistFile = nullptr;
            hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
            
            if (SUCCEEDED(hr)) {
                hr = pPersistFile->Load(shortcutPath.c_str(), STGM_READ);
                
                if (SUCCEEDED(hr)) {
                    wchar_t targetBuffer[MAX_PATH] = {};
                    hr = pShellLink->GetPath(targetBuffer, MAX_PATH, nullptr, 0);
                    
                    if (SUCCEEDED(hr)) {
                        targetPath = targetBuffer;
                    }
                }
                
                pPersistFile->Release();
            }
            
            pShellLink->Release();
        }
    } catch (...) {
        // Exception occurred
    }
    
    CoUninitialize();
    return targetPath;
}
