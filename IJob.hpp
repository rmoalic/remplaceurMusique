#pragma once
#include <cstdint>

class IJob
{
public:
    virtual ~IJob() = default;
    virtual void Start() = 0;
    virtual void RequestStop() = 0;
    virtual bool StopRequested() const = 0;
    virtual bool IsFinished() const = 0;
    virtual void Join() = 0;
    virtual uint64_t CurrentGeneration() const = 0;
};