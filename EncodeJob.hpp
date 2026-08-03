#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "VideoEncoder.hpp"   // EncodeCallbacks

struct EncodeParams;


class EncodeJob
{
public:
    static std::unique_ptr<EncodeJob> Start(
        std::unique_ptr<EncodeParams> params, EncodeCallbacks callbacks);

    void RequestCancel();

    bool CancelRequested() const;

    bool IsFinished() const;

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