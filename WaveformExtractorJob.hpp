#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <functional>
#include "WaveformExtractor.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// WaveformExtractorJob
//
// Owns every background waveform-extraction thread. No HWND, no
// PostMessage: the result is delivered through the callback passed to
// Start(), invoked from the worker thread — marshal to a UI thread inside
// the callback if needed (that's Win32/WinUI/whatever-specific glue, and
// belongs at the call site, not here).
//
// Only the most recent request matters: each Start() bumps a generation
// counter, and any in-flight extraction for an older generation notices and
// discards its result instead of invoking the callback — so picking several
// audio files quickly never races a stale waveform onto the UI.
//
//   static WaveformExtractorJob g_waveform;
//   ...
//   g_waveform.Start(path, duration, [](uint64_t gen, std::vector<float> values) {
//       // values.empty() means extraction failed. Marshal to the UI thread
//       // here if this callback isn't already running on it.
//   });
//   ...
//   // shutdown:
//   g_waveform.RequestStop();
//   ...
//   g_waveform.Join();
//
// Intended to be driven from one thread only (Start/RequestStop/Join are not
// safe to call concurrently with each other).
// ─────────────────────────────────────────────────────────────────────────────
class WaveformExtractorJob
{
public:
    using ReadyCallback = std::function<void(uint64_t generation, std::vector<float> values)>;

    explicit WaveformExtractorJob(int sampleCount = 700, int sampleRateHz = 8000);
    ~WaveformExtractorJob();

    // Starts extracting `path`'s waveform on a background thread. Calls
    // onReady on completion — values.empty() means the file couldn't be
    // opened/decoded. Returns the generation assigned to this request;
    // compare it against CurrentGeneration() to know if it's still current.
    // Not called at all if superseded or RequestStop() was called first.
    uint64_t Start(std::wstring path, double durationSecs, ReadyCallback onReady);

    // Generation of the most recent Start() call. Callbacks for any older
    // generation are already suppressed internally; exposed in case the
    // caller wants to double check on its own thread too.
    uint64_t CurrentGeneration() const {
        return m_generation.load();
    }

    // Tells every in-flight extraction to stop at its next check and stop
    // invoking further callbacks. Does not block.
    void RequestStop();

    // Joins every background thread started so far.
    void Join();

private:
    void RunOnThread(std::wstring path, uint64_t generation, double durationSecs, ReadyCallback onReady);

    WaveformExtractor m_extractor;
    std::atomic<uint64_t> m_generation{ 0 };
    std::atomic<bool> m_stopRequested{ false };
    std::vector<std::thread> m_threads;
};