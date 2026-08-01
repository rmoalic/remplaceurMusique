#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <climits>
#include <memory>
#include <string>
#include <atomic>
#include "Encode.hpp"

using Microsoft::WRL::ComPtr;

double MF_GetDuration(const std::wstring& path);

struct Hns
{
    LONGLONG value = 0;
    constexpr Hns() = default;
    explicit constexpr Hns(LONGLONG v) : value(v) {}
    static Hns FromSeconds(double s) {
        return Hns((LONGLONG)(s * 1e7));
    }
    static constexpr Hns Max() {
        return Hns(LLONG_MAX);
    }

    bool operator< (Hns o) const {
        return value < o.value;
    }
    bool operator<=(Hns o) const {
        return value <= o.value;
    }
    bool operator> (Hns o) const {
        return value > o.value;
    }
    bool operator>=(Hns o) const {
        return value >= o.value;
    }
    bool operator==(Hns o) const {
        return value == o.value;
    }
    Hns  operator+ (Hns o) const {
        return Hns(value + o.value);
    }
    Hns  operator- (Hns o) const {
        return Hns(value - o.value);
    }
};

struct VideoSourceInfo
{
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType>    actualType;
    UINT32 width = 0, height = 0;
    UINT32 frNum = 30, frDen = 1;
    UINT32 sourceBitrate = 0;
    UINT32 outW = 0, outH = 0;
};

struct AudioSourceInfo
{
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType>    actualType;
    UINT32 nbChannels = 0;
    UINT32 bytesPerSec = 0;
};

constexpr DWORD kInvalidStreamIndex = static_cast<DWORD>(-1);

struct VideoStreamConfig
{
    UINT32 maxBitrate = 0;
    UINT32 h264Profile = 0;
    UINT32 maxWidth = 0;
    UINT32 maxHeight = 0;
    Hns    startHns = Hns(0);
    Hns    maxDurHns = Hns::Max();
};

struct AudioStreamConfig
{
    UINT32 channels = 0;
    UINT32 bytesPerSec = 0;
    Hns    startHns = Hns(0);
    Hns    rangeHns = Hns::Max();
};

struct EncodeLoop
{
    // Video pump
    LONGLONG vidTimeBase = -1;
    LONGLONG vidLastTs = 0;
    bool     vidDone = false;
    int      nullStreak = 0;

    // Audio pump
    ComPtr<IMFSourceReader> audReader;   // owned here so loop can reopen it
    LONGLONG audWritten = 0;
    LONGLONG audPosInRange = 0;
    bool     audEOF = false;

    LONGLONG     lastProgressHns = 0;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER qpcStart{};
};

enum class FrameResult { Continue, Done, Error };

class VideoEncoder
{
public:
    static std::unique_ptr<VideoEncoder> Create(
        std::unique_ptr<EncodeParams> params,
        std::shared_ptr<std::atomic_bool> cancelRequested);

    void Run();

private:
    VideoEncoder(
        HWND hWnd, std::wstring outputPath, float volumeScale, AudioShortMode audioRepeat,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        ComPtr<IMFDXGIDeviceManager> devMgr, ComPtr<IMFSinkWriter> writer,
        std::unique_ptr<VideoSourceInfo> vid, std::unique_ptr<AudioSourceInfo> aud,
        DWORD vidIdx, DWORD audIdx,
        VideoStreamConfig vcfg, AudioStreamConfig acfg, Hns outputDurHns);

    static ComPtr<IMFDXGIDeviceManager> CreateD3DManager();

    static std::unique_ptr<VideoSourceInfo> OpenVideoReader(
        const ComPtr<IMFDXGIDeviceManager>& devMgr, const std::wstring& vfile,
        const VideoStreamConfig& cfg, std::wstring& err);

    static std::unique_ptr<AudioSourceInfo> OpenAudioReader(
        const std::wstring& afile, const AudioStreamConfig& cfg, std::wstring& err);

    static ComPtr<IMFSinkWriter> CreateSinkWriter(
        const ComPtr<IMFDXGIDeviceManager>& devMgr, const std::wstring& outfile, std::wstring& err);

    static DWORD ConfigureVideoStream(
        const VideoSourceInfo& vid, IMFSinkWriter* writer, const VideoStreamConfig& cfg, std::wstring& err);

    static DWORD ConfigureAudioStream(
        const AudioSourceInfo& aud, IMFSinkWriter* writer, const AudioStreamConfig& cfg, std::wstring& err);

    static bool SeekVideoToStart(IMFSourceReader* reader, Hns start, std::wstring& err);

    // ── Encode-loop helpers ───────────────────────────────────────────────────
    FrameResult ProcessVideoFrame(EncodeLoop& loop, IMFSinkWriter* writer, DWORD vidIdx, std::wstring& err);
    bool ProcessAudio(EncodeLoop& loop, IMFSinkWriter* writer, DWORD audIdx, std::wstring& err);
    void ReportProgress(EncodeLoop& loop, LONGLONG relHns, LONGLONG maxDurHns);

    // ── Runtime error / cancellation (post-construction only) ────────────────
    void ReportFailure(const std::wstring& msg);
    bool IsCancellationRequested() const;
    void Cancel();

    // ── Members (all set once, at construction, by Create()) ─────────────────
    HWND  m_hWnd;
    std::wstring m_outputPath;
    float m_volumescale;
    AudioShortMode m_audio_repeat;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;

    ComPtr<IMFDXGIDeviceManager>  m_devMgr;
    ComPtr<IMFSinkWriter>         m_writer;
    std::unique_ptr<VideoSourceInfo> m_vid;
    std::unique_ptr<AudioSourceInfo> m_aud;
    DWORD m_vidIdx;
    DWORD m_audIdx;

    VideoStreamConfig m_vcfg;
    AudioStreamConfig m_acfg;
    Hns m_outputDurHns;

    static constexpr LONGLONG kPcmHnsDen = 10000000LL;
    LONGLONG m_pcmBytesNum;
};
