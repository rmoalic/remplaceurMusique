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
#include <functional>
#include "Encode.hpp"

using Microsoft::WRL::ComPtr;



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

enum class EncodeError
{
    None = 0,

    // Init: video source
    VideoOpenFailed,
    VideoStreamSelectFailed,
    VideoDecodeFailed,
    VideoTypeReadFailed,
    VideoDimensionsInvalid,

    // Init: audio source
    AudioOpenFailed,
    AudioStreamSelectFailed,
    AudioFormatFailed,
    AudioSeekFailed,
    AudioTypeReadFailed,

    // Init: sink writer / streams
    OutputCreateFailed,
    VideoStreamAddFailed,
    VideoStreamTypeIncompatible,
    AudioStreamAddFailed,
    AudioStreamConfigFailed,
    VideoSeekFailed,

    // Run
    SinkWriterBeginFailed,
    SinkWriterFinalizeFailed,
    VideoReadFailed,
    VideoWriteFailed,
    AudioReadFailed,
    AudioWriteFailed,
    AudioSilenceWriteFailed,
    AudioLoopRestartFailed,

    Cancelled,
    Unknown,
};

struct EncodeErrorInfo
{
    EncodeError code = EncodeError::None;
    HRESULT hr = S_OK;

    EncodeErrorInfo() = default;
    EncodeErrorInfo(EncodeError c, HRESULT h = S_OK) : code(c), hr(h) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// VideoSourceInfo  — produced by OpenVideoReader(), consumed by ConfigureVideoStream()
// ─────────────────────────────────────────────────────────────────────────────
struct VideoSourceInfo
{
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType>    actualType;  // negotiated decode format (NV12/P010/YUY2)
    UINT32 width = 0, height = 0;
    UINT32 frNum = 30, frDen = 1;
    UINT32 sourceBitrate = 0;
    UINT32 outW = 0, outH = 0;     // scaled + H.264-aligned output dimensions
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
    Hns    startHns = Hns(0);     // seek point for the VIDEO reader
    Hns    maxDurHns = Hns::Max(); // trim: stop once (ts - start) reaches this
};

struct AudioStreamConfig
{
    UINT32 channels = 0;
    UINT32 bytesPerSec = 0;
    Hns    startHns = Hns(0);      // seek point AND loop-restart point for the AUDIO reader
    Hns    rangeHns = Hns::Max();  // trim: length of the selected audio range
};

struct EncodeCallbacks
{
    // pct in [0, 100), etaSecs is the estimated remaining time.
    std::function<void(int pct, double etaSecs)> onProgress;

    // Called exactly once, whether the encode succeeded, failed, or was
    // cancelled. error.code is EncodeError::None when ok is true.
    std::function<void(bool ok, EncodeErrorInfo error)> onDone;
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

    // Progress / ETA
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
        std::shared_ptr<std::atomic_bool> cancelRequested,
        EncodeCallbacks callbacks);

    void Run();

private:
    // Only Create() may build one of these; every instance is fully valid.
    VideoEncoder(
        std::wstring outputPath, float volumeScale, AudioShortMode audioRepeat,
        std::shared_ptr<std::atomic_bool> cancelRequested, EncodeCallbacks callbacks,
        ComPtr<IMFDXGIDeviceManager> devMgr, ComPtr<IMFSinkWriter> writer,
        std::unique_ptr<VideoSourceInfo> vid, std::unique_ptr<AudioSourceInfo> aud,
        DWORD vidIdx, DWORD audIdx,
        VideoStreamConfig vcfg, AudioStreamConfig acfg, Hns outputDurHns);

    // ── Init helpers – static: they only get what they need, and report
    //    failure through the `err` out-param instead of a hidden side channel.
    static ComPtr<IMFDXGIDeviceManager> CreateD3DManager();

    static std::unique_ptr<VideoSourceInfo> OpenVideoReader(
        const ComPtr<IMFDXGIDeviceManager>& devMgr, const std::wstring& vfile,
        const VideoStreamConfig& cfg, EncodeErrorInfo& err);

    static std::unique_ptr<AudioSourceInfo> OpenAudioReader(
        const std::wstring& afile, const AudioStreamConfig& cfg, EncodeErrorInfo& err);

    static ComPtr<IMFSinkWriter> CreateSinkWriter(
        const ComPtr<IMFDXGIDeviceManager>& devMgr, const std::wstring& outfile, EncodeErrorInfo& err);

    static DWORD ConfigureVideoStream(
        const VideoSourceInfo& vid, IMFSinkWriter* writer, const VideoStreamConfig& cfg, EncodeErrorInfo& err);

    static DWORD ConfigureAudioStream(
        const AudioSourceInfo& aud, IMFSinkWriter* writer, const AudioStreamConfig& cfg, EncodeErrorInfo& err);

    static bool SeekVideoToStart(IMFSourceReader* reader, Hns start, EncodeErrorInfo& err);

    // ── Encode-loop helpers ───────────────────────────────────────────────────
    FrameResult ProcessVideoFrame(EncodeLoop& loop, IMFSinkWriter* writer, DWORD vidIdx, EncodeErrorInfo& err);
    bool ProcessAudio(EncodeLoop& loop, IMFSinkWriter* writer, DWORD audIdx, EncodeErrorInfo& err);
    void ReportProgress(EncodeLoop& loop, LONGLONG relHns, LONGLONG maxDurHns);

    // ── Runtime error / cancellation (post-construction only) ────────────────
    void ReportFailure(EncodeErrorInfo err);
    bool IsCancellationRequested() const;
    void Cancel();

    // ── Members (all set once, at construction, by Create()) ─────────────────
    std::wstring m_outputPath;
    float m_volumescale;
    AudioShortMode m_audio_repeat;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    EncodeCallbacks m_callbacks;

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