#include "EncodeJob.hpp"
#include "VideoEncoder.hpp"
#include "Encode.hpp"

EncodeJob::EncodeJob()
    : m_cancelRequested(std::make_shared<std::atomic_bool>(false))
{
}

EncodeJob::~EncodeJob()
{
    Join();
}

std::unique_ptr<EncodeJob> EncodeJob::Start(std::unique_ptr<EncodeParams> params)
{
    auto job = std::unique_ptr<EncodeJob>(new EncodeJob());
    auto cancelFlag = job->m_cancelRequested;
    auto* finishedFlag = &job->m_finished;

    job->m_thread = std::thread(
                        [p = std::move(params), cancelFlag, finishedFlag]() mutable
    {
        auto encoder = VideoEncoder::Create(std::move(p), cancelFlag);
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