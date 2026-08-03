#pragma once
#include "Encode.hpp"

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

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// Hns — a duration or absolute position expressed in 100ns units (the native
// unit throughout Media Foundation). Only the GUI layer deals in seconds;
// everything past the boundary is Hns. Use Hns::FromSeconds() at that boundary
// and nowhere else.
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// Per-stream configuration. These exist so that call sites are self-
// documenting: passing a VideoStreamConfig where an AudioStreamConfig is
// expected is a compile error, unlike passing a bare LONGLONG in the wrong
// argument slot.
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// EncodeCallbacks — everything VideoEncoder reports to the outside world.
// No HWND, no window messages: whoever calls Create() decides what "report
// progress" and "report completion" mean (post a Win32 message, update an
// observable property, write to a log, whatever). Both are invoked from the
// worker thread — marshal to a UI thread inside the callback if needed.
// ─────────────────────────────────────────────────────────────────────────────
struct EncodeCallbacks
{
    // pct in [0, 100), etaSecs is the estimated remaining time.
    std::function<void(int pct, double etaSecs)> onProgress;

    // Called exactly once, whether the encode succeeded, failed, or was
    // cancelled. error is empty when ok is true.
    std::function<void(bool ok, std::wstring error)> onDone;
};

// ─────────────────────────────────────────────────────────────────────────────
// EncodeLoop  — mutable state that only lives inside VideoEncoder::Run()
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// VideoEncoder
//
// Construction can fail (bad files, unsupported formats, ...), so there is no
// public constructor: use Create(), which returns nullptr on failure and has
// already invoked callbacks.onDone(false, ...). Any VideoEncoder you hold is
// therefore guaranteed fully configured and safe to Run().
//
//
//   auto encoder = VideoEncoder::Create(std::move(params), cancelFlag, callbacks);
//   if (!encoder) return;   // failure was already reported via callbacks.onDone
//   encoder->Run();
// ─────────────────────────────────────────────────────────────────────────────
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