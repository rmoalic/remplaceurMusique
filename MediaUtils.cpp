#include "MediaUtils.hpp"
#include "StringUtils.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Import MF_GetDuration from VideoEncoder.cpp
extern "C" double MF_GetDuration(const std::wstring& path);

// ===========================================================================
// Media Duration Functions
// ===========================================================================

double MediaUtils::GetMediaDuration(const std::wstring& filePath)
{
    return MF_GetDuration(filePath);
}

// ===========================================================================
// Media Information Functions
// ===========================================================================

bool MediaUtils::GetVideoDimensions(const std::wstring& filePath, int& width, int& height)
{
    if (filePath.empty()) return false;
    
    width = 0;
    height = 0;
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return false;
    
    // Try to get video stream
    ComPtr<IMFMediaType> pMediaType;
    
    // Try first video stream
    for (DWORD i = 0; ; i++) {
        hr = pReader->GetNativeMediaType(i, 0, &pMediaType);
        if (FAILED(hr)) break;
        
        GUID majorType;
        hr = pMediaType->GetMajorType(&majorType);
        if (FAILED(hr)) continue;
        
        if (majorType == MFMediaType_Video) {
            hr = MFGetAttributeSize(pMediaType.Get(), MF_MT_FRAME_SIZE, reinterpret_cast<UINT32*>(&width), reinterpret_cast<UINT32*>(&height));
            if (SUCCEEDED(hr)) {
                return true;
            }
        }
    }
    
    return false;
}

int MediaUtils::GetAudioChannels(const std::wstring& filePath)
{
    if (filePath.empty()) return 0;
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return 0;
    
    // Try to get audio stream
    ComPtr<IMFMediaType> pMediaType;
    
    for (DWORD i = 0; ; i++) {
        hr = pReader->GetNativeMediaType(i, 0, &pMediaType);
        if (FAILED(hr)) break;
        
        GUID majorType;
        hr = pMediaType->GetMajorType(&majorType);
        if (FAILED(hr)) continue;
        
        if (majorType == MFMediaType_Audio) {
            UINT32 channels = 0;
            hr = pMediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            if (SUCCEEDED(hr)) {
                return static_cast<int>(channels);
            }
        }
    }
    
    return 0;
}

int MediaUtils::GetAudioSampleRate(const std::wstring& filePath)
{
    if (filePath.empty()) return 0;
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return 0;
    
    // Try to get audio stream
    ComPtr<IMFMediaType> pMediaType;
    
    for (DWORD i = 0; ; i++) {
        hr = pReader->GetNativeMediaType(i, 0, &pMediaType);
        if (FAILED(hr)) break;
        
        GUID majorType;
        hr = pMediaType->GetMajorType(&majorType);
        if (FAILED(hr)) continue;
        
        if (majorType == MFMediaType_Audio) {
            UINT32 sampleRate = 0;
            hr = pMediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
            if (SUCCEEDED(hr)) {
                return static_cast<int>(sampleRate);
            }
        }
    }
    
    return 0;
}

int MediaUtils::GetAudioBitRate(const std::wstring& filePath)
{
    if (filePath.empty()) return 0;
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return 0;
    
    // Try to get audio stream
    ComPtr<IMFMediaType> pMediaType;
    
    for (DWORD i = 0; ; i++) {
        hr = pReader->GetNativeMediaType(i, 0, &pMediaType);
        if (FAILED(hr)) break;
        
        GUID majorType;
        hr = pMediaType->GetMajorType(&majorType);
        if (FAILED(hr)) continue;
        
        if (majorType == MFMediaType_Audio) {
            UINT32 bitRate = 0;
            hr = pMediaType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bitRate);
            if (SUCCEEDED(hr)) {
                return static_cast<int>(bitRate * 8); // Convert to bits per second
            }
        }
    }
    
    return 0;
}

// ===========================================================================
// Media Format Functions
// ===========================================================================

bool MediaUtils::IsValidMediaFile(const std::wstring& filePath)
{
    if (filePath.empty()) return false;
    
    // Check by extension first (quick check)
    std::wstring ext = StringUtils::GetFileExtension(filePath);
    ext = StringUtils::ToLower(ext);
    
    static const wchar_t* mediaExtensions[] = {
        L"mp4", L"mov", L"avi", L"mkv", L"m4v", L"wmv", L"flv", L"webm",
        L"mp3", L"wav", L"aac", L"flac", L"ogg", L"m4a", L"wma", L"alac"
    };
    
    for (const wchar_t* extName : mediaExtensions) {
        if (ext == extName) {
            return true;
        }
    }
    
    // Try to open with Media Foundation (slower but more reliable)
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    return SUCCEEDED(hr);
}

std::wstring MediaUtils::GetMediaContainerFormat(const std::wstring& filePath)
{
    if (filePath.empty()) return L"";
    
    std::wstring ext = StringUtils::GetFileExtension(filePath);
    return StringUtils::ToLower(ext);
}

std::wstring MediaUtils::GetVideoCodec(const std::wstring& filePath)
{
    if (filePath.empty()) return L"";
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return L"";
    
    // Try to get video stream
    ComPtr<IMFMediaType> pMediaType;
    
    for (DWORD i = 0; ; i++) {
        hr = pReader->GetNativeMediaType(i, 0, &pMediaType);
        if (FAILED(hr)) break;
        
        GUID majorType, subType;
        hr = pMediaType->GetMajorType(&majorType);
        if (FAILED(hr)) continue;
        
        if (majorType == MFMediaType_Video) {
            hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subType);
            if (SUCCEEDED(hr)) {
                // Convert GUID to string representation
                if (subType == MFVideoFormat_H264) return L"H.264";
                if (subType == MFVideoFormat_HEVC) return L"H.265/HEVC";
                if (subType == MFVideoFormat_VP9) return L"VP9";
                if (subType == MFVideoFormat_VP8) return L"VP8";
                if (subType == MFVideoFormat_MPEG2) return L"MPEG-2";
                if (subType == MFVideoFormat_MJPEG) return L"MJPEG";
                if (subType == MFVideoFormat_NV12) return L"NV12";
                if (subType == MFVideoFormat_YUY2) return L"YUY2";
                
                // Return GUID as string
                LPOLESTR guidStr = nullptr;
                if (SUCCEEDED(StringFromCLSID(subType, &guidStr))) {
                    std::wstring result(guidStr);
                    CoTaskMemFree(guidStr);
                    return result;
                }
            }
        }
    }
    
    return L"";
}

std::wstring MediaUtils::GetAudioCodec(const std::wstring& filePath)
{
    if (filePath.empty()) return L"";
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return L"";
    
    // Try to get audio stream
    ComPtr<IMFMediaType> pMediaType;
    
    for (DWORD i = 0; ; i++) {
        hr = pReader->GetNativeMediaType(i, 0, &pMediaType);
        if (FAILED(hr)) break;
        
        GUID majorType, subType;
        hr = pMediaType->GetMajorType(&majorType);
        if (FAILED(hr)) continue;
        
        if (majorType == MFMediaType_Audio) {
            hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subType);
            if (SUCCEEDED(hr)) {
                // Convert GUID to string representation
                if (subType == MFAudioFormat_AAC) return L"AAC";
                if (subType == MFAudioFormat_MP3) return L"MP3";
                if (subType == MFAudioFormat_PCM) return L"PCM";
                if (subType == MFAudioFormat_Float) return L"Float";
                if (subType == MFAudioFormat_Dolby_AC3) return L"AC3";
                if (subType == MFAudioFormat_WMAudioV8) return L"WMA";
                if (subType == MFAudioFormat_WMAudioV9) return L"WMA";
                if (subType == MFAudioFormat_FLAC) return L"FLAC";
                
                // Return GUID as string
                LPOLESTR guidStr = nullptr;
                if (SUCCEEDED(StringFromCLSID(subType, &guidStr))) {
                    std::wstring result(guidStr);
                    CoTaskMemFree(guidStr);
                    return result;
                }
            }
        }
    }
    
    return L"";
}

// ===========================================================================
// Media Thumbnail Functions
// ===========================================================================

bool MediaUtils::ExtractVideoThumbnail(const std::wstring& videoPath, const std::wstring& outputPath,
                                     int width, int height, double position)
{
    // This would require more complex Media Foundation setup
    // For now, return false as this is not implemented
    return false;
}

// ===========================================================================
// Media Metadata Functions
// ===========================================================================

bool MediaUtils::GetMediaMetadata(const std::wstring& filePath, std::wstring& title,
                                  std::wstring& artist, std::wstring& album)
{
    if (filePath.empty()) return false;
    
    title = L"";
    artist = L"";
    album = L"";
    
    ComPtr<IMFSourceReader> pReader;
    HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &pReader);
    if (FAILED(hr)) return false;
    
    // Try to get metadata from the source reader
    ComPtr<IMFAttributes> pAttributes;
    hr = pReader->GetServiceForStream(MF_SOURCE_READER_MEDIASOURCE, GUID_NULL, IID_PPV_ARGS(&pAttributes));
    if (FAILED(hr)) return false;
    
    // Try to get title
    PROPVARIANT var;
    PropVariantInit(&var);
    
    if (SUCCEEDED(pAttributes->GetString(MF_METADATA_TITLE, var.pwszVal, &var.caub))) {
        title = var.pwszVal;
    }
    PropVariantClear(&var);
    
    // Try to get artist
    PropVariantInit(&var);
    if (SUCCEEDED(pAttributes->GetString(MF_METADATA_AUTHOR, var.pwszVal, &var.caub))) {
        artist = var.pwszVal;
    }
    PropVariantClear(&var);
    
    // Try to get album
    PropVariantInit(&var);
    if (SUCCEEDED(pAttributes->GetString(MF_METADATA_ALBUM_NAME, var.pwszVal, &var.caub))) {
        album = var.pwszVal;
    }
    PropVariantClear(&var);
    
    return !title.empty() || !artist.empty() || !album.empty();
}

// ===========================================================================
// Media Conversion Utilities
// ===========================================================================

bool MediaUtils::ConvertVideo(const std::wstring& inputPath, const std::wstring& outputPath,
                             const std::wstring& outputFormat, int quality)
{
    // This would require Media Foundation transform or other encoding infrastructure
    // For now, return false as this is not implemented
    return false;
}

bool MediaUtils::ExtractAudio(const std::wstring& videoPath, const std::wstring& outputPath,
                             const std::wstring& format)
{
    // This would require Media Foundation audio extraction
    // For now, return false as this is not implemented
    return false;
}
