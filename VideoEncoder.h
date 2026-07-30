#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <climits>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// VideoSourceInfo  — produced by OpenVideoReader(), consumed by ConfigureVideoStream()
// ─────────────────────────────────────────────────────────────────────────────
struct VideoSourceInfo
{
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType>    actualType;  // negotiated decode format (NV12/P010/YUY2)
    UINT32 width = 0, height = 0;
    UINT32 frNum = 30, frDen = 1;
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

// ─────────────────────────────────────────────────────────────────────────────
// VideoEncoder
//
// Thin orchestrator: each Init* free-standing method receives only what it
// needs and returns only what it produces. The class itself holds the three
// things that genuinely span the whole lifetime:
//   - the job parameters
//   - the DXGI device manager (shared by reader + writer)
//   - the configured sink writer + stream indices
//
// Typical use:
//   VideoEncoder encoder(std::move(params));
//   if (!encoder.Initialize()) return;
//   encoder.Run();
// ─────────────────────────────────────────────────────────────────────────────
class VideoEncoder
{
public:
    explicit VideoEncoder(std::unique_ptr<EncodeParams> params);

    bool Initialize();
    void Run();

private:
    // ── Init helpers – each returns its product, nullptr/false on failure ────
    //    Failures call Fail() internally before returning.

    // Step 0 – optional GPU acceleration context (non-fatal, may return null)
    ComPtr<IMFDXGIDeviceManager> CreateD3DManager();

    // Step 1 – video source reader + negotiated format metadata
    std::unique_ptr<VideoSourceInfo> OpenVideoReader(
        const ComPtr<IMFDXGIDeviceManager>& devMgr, UINT32 max_out_width, UINT32 max_out_height);

    // Step 2 – audio source reader (delegates to ::OpenAudioReader)
    std::unique_ptr<AudioSourceInfo> OpenAudioReader();

    // Step 3 – sink writer for the output MP4
    ComPtr<IMFSinkWriter> CreateSinkWriter(
        const ComPtr<IMFDXGIDeviceManager>& devMgr);

    // Step 4 – add H.264 stream, return assigned stream index
    DWORD ConfigureVideoStream(
        const VideoSourceInfo vid, IMFSinkWriter* writer, UINT32 max_vid_bitrate, UINT32 h264Profile);
    
    // Step 5 – add AAC stream, return assigned stream index
    DWORD ConfigureAudioStream(const AudioSourceInfo aud,  IMFSinkWriter* writer, UINT32 nb_channels, UINT32 bytes_per_sec);

    // Step 6 – seek to videoStart (no-op when <= 0)
    void SeekVideoToStart(IMFSourceReader* reader);

    // ── Encode-loop helpers (all take explicit state, nothing implicit) ──────

    // Read + write one video frame; updates loop state. Returns false when done.
    bool ProcessVideoFrame(EncodeLoop& loop, IMFSinkWriter* writer, DWORD vidIdx);

    // Write audio to stay ~200 ms ahead of video; handles looping + silence.
    void ProcessAudio(EncodeLoop& loop, IMFSinkWriter* writer, DWORD audIdx,
        LONGLONG audioRangeHns, LONGLONG maxDurHns);

    // Progress notification (only called when maxDurHns is known)
    void ReportProgress(EncodeLoop& loop, LONGLONG relHns, LONGLONG maxDurHns);

    // ── Error helper ─────────────────────────────────────────────────────────
    void Fail(const wchar_t* msg);

    // ── Members (only what spans the full object lifetime) ───────────────────
    std::unique_ptr<EncodeParams> m_params;

    // Produced by Initialize(), consumed by Run()
    ComPtr<IMFDXGIDeviceManager>  m_devMgr;
    ComPtr<IMFSinkWriter>         m_writer;
    DWORD                         m_vidIdx = 0;
    DWORD                         m_audIdx = 1;

    // Precomputed from params (read-only after ctor)
    LONGLONG m_maxDurHns = LLONG_MAX;
    LONGLONG m_audioRangeHns = LLONG_MAX;
    LONGLONG m_pcmBytesNum = 0;
    static constexpr LONGLONG kPcmHnsDen = 10000000LL;

    // Kept for Run() to pass into ProcessVideoFrame
    std::unique_ptr<VideoSourceInfo> m_vid;
    std::unique_ptr<AudioSourceInfo> m_aud;
};
