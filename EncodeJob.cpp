#include "EncodeJob.hpp"
#include "VideoEncoder.hpp"
#include "Encode.hpp"   // EncodeParams

EncodeJob::EncodeJob()
    : m_cancelRequested(std::make_shared<std::atomic_bool>(false))
{
}

EncodeJob::~EncodeJob()
{
    Join();
}

std::unique_ptr<EncodeJob> EncodeJob::Start(
    std::unique_ptr<EncodeParams> params, EncodeCallbacks callbacks)
{
    // unique_ptr<EncodeJob>(new EncodeJob()) since the constructor is private;
    // Start() is the only place allowed to call it, same reasoning as
    // VideoEncoder::Create().
    auto job = std::unique_ptr<EncodeJob>(new EncodeJob());

    // Capture the flag by value (shared_ptr), not `job` itself: the thread
    // must not touch the EncodeJob object, since it can be destroyed (and
    // Join()'d) from the caller's thread while the worker is still running.
    auto cancelFlag = job->m_cancelRequested;
    auto* finishedFlag = &job->m_finished;

    job->m_thread = std::thread(
                        [p = std::move(params), cancelFlag, finishedFlag, cb = std::move(callbacks)]() mutable
    {
        auto encoder = VideoEncoder::Create(std::move(p), cancelFlag, std::move(cb));
        if (encoder) encoder->Run();
        finishedFlag->store(true);
    });

    return job;
}

void EncodeJob::RequestCancel()
{
    m_cancelRequested->store(true);
}

bool EncodeJob::CancelRequested() const
{
    return m_cancelRequested->load();
}

bool EncodeJob::IsFinished() const
{
    return m_finished.load();
}

void EncodeJob::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}