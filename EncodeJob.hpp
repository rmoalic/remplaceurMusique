#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "VideoEncoder.hpp"   // EncodeCallbacks

// Forward-declared; defined wherever EncodeParams already lives (Encode.hpp).
struct EncodeParams;

// ─────────────────────────────────────────────────────────────────────────────
// EncodeJob
//
// Owns one background encode: the worker thread and the cancellation flag it
// polls. This is the only object the caller needs to hold onto. No HWND
// anywhere — progress/completion are reported through the EncodeCallbacks
// passed to Start(), same as VideoEncoder.
//
//   auto job = EncodeJob::Start(std::move(params), {
//       .onProgress = [](int pct, double eta) { ... },
//       .onDone     = [](bool ok, std::wstring err) { ... },
//   });
//   ...
//   job->RequestCancel();   // safe to call from any thread
//   ...
//   job.reset();            // or let it go out of scope: joins automatically
// ─────────────────────────────────────────────────────────────────────────────
class EncodeJob
{
public:
    static std::unique_ptr<EncodeJob> Start(
        std::unique_ptr<EncodeParams> params, EncodeCallbacks callbacks);

    // Signals the worker to stop. Safe to call from any thread, any number
    // of times. Cancellation is cooperative: the worker finishes its current
    // step, deletes the partial output, and calls callbacks.onDone(false, ...)
    // — RequestCancel() itself does not block.
    void RequestCancel();

    // True once the flag has been raised (does not mean the thread has
    // finished yet — Join() / the destructor / IsFinished() tell you that).
    bool CancelRequested() const;

    // True once the worker thread has run to completion.
    bool IsFinished() const;

    // Joins the worker thread if still running. Also called by the
    // destructor, so destroying/resetting the EncodeJob is always safe and
    // never leaves a detached thread behind.
    void Join();

    ~EncodeJob();

    EncodeJob(const EncodeJob&) = delete;
    EncodeJob& operator=(const EncodeJob&) = delete;

private:
    EncodeJob();

    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    std::atomic_bool m_finished{ false };
    std::thread m_thread;
};