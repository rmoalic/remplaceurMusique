#pragma once
#include <string>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// WaveformExtractor
//
// Pure logic: given an audio file, produce a fixed-size array of per-bucket
// peak amplitudes (0..1). Knows nothing about HWND, threads, or messages —
// it can be unit tested against a file and a stop predicate alone.
//
//   WaveformExtractor extractor(700, 8000);
//   auto samples = extractor.Extract(path, durationSecs, [] { return false; });
//   if (samples.empty()) { /* failed to open/decode */ }
// ─────────────────────────────────────────────────────────────────────────────
class WaveformExtractor
{
public:
    explicit WaveformExtractor(int sampleCount = 700, int sampleRateHz = 8000);

    // Synchronous. Decodes `path` as mono PCM and returns exactly
    // sampleCount() peak-amplitude buckets (0..1), covering durationSecs of
    // audio (pass 0 if unknown — extraction still works, just without
    // pre-sized buckets from a known duration).
    //
    // Returns an empty vector if the file couldn't be opened/decoded, or if
    // shouldStop() returns true before extraction finishes — the caller
    // can't tell those two apart from the return value alone, and generally
    // shouldn't need to (both mean "no result to show").
    //
    // shouldStop is polled once per decoded sample buffer; pass a no-op
    // lambda ([]{ return false; }) if you don't need cancellation.
    std::vector<float> Extract(
        const std::wstring& path, double durationSecs,
        const std::function<bool()>& shouldStop) const;

    int sampleCount() const {
        return m_sampleCount;
    }
    int sampleRateHz() const {
        return m_sampleRateHz;
    }

private:
    int m_sampleCount;
    int m_sampleRateHz;
};