#pragma once
#include "AbstractJob.hpp"
#include <memory>
#include <atomic>
#include "VideoEncoder.hpp"
struct EncodeParams;

class EncodeJob : public AbstractJob
{
public:
    EncodeJob(std::unique_ptr<EncodeParams> params, EncodeCallbacks callbacks);
    ~EncodeJob() override;

protected:
    void run(std::stop_token stopToken, uint64_t generation) override;

private:
    std::unique_ptr<EncodeParams> m_params;
    EncodeCallbacks m_callbacks;
};