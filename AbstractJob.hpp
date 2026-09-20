#pragma once
#include "IJob.hpp"
#include <thread>
#include <stop_token>
#include <atomic>
#include <mutex>
#include <cstdint>

class AbstractJob : public IJob
{
public:
    AbstractJob() = default;
    virtual ~AbstractJob()
    {
        RequestStop();
        Join();
    }

    void Start() override
    {
        std::lock_guard<std::mutex> lk(m_startMutex);
        if (m_running.load(std::memory_order_acquire))
            return;

        ++m_generation;
        m_finished.store(false, std::memory_order_release);
        m_stopRequested.store(false, std::memory_order_release);
        m_running.store(true, std::memory_order_release);

        m_thread = std::jthread([this](std::stop_token st) {
            const uint64_t gen = m_generation.load(std::memory_order_acquire);
            run(st, gen);
            m_finished.store(true, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
        });
    }

    void RequestStop() override
    {
        m_stopRequested.store(true, std::memory_order_release);
        if (m_thread.joinable()) {
            m_thread.request_stop();
        }
    }

    bool StopRequested() const override
    {
        return m_stopRequested.load(std::memory_order_acquire) ||
               (m_thread.joinable() && m_thread.get_stop_token().stop_requested());
    }

    bool IsFinished() const override
    {
        return m_finished.load(std::memory_order_acquire);
    }

    void Join() override
    {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    uint64_t CurrentGeneration() const override
    {
        return m_generation.load(std::memory_order_acquire);
    }

protected:
    // Implémenté par les jobs concrets. Doit vérifier régulièrement stopToken.
    virtual void run(std::stop_token stopToken, uint64_t generation) = 0;

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

private:
    std::jthread m_thread;
    std::atomic<uint64_t> m_generation{ 0 };
    std::atomic<bool> m_finished{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::atomic<bool> m_running{ false };
    std::mutex m_startMutex;
};