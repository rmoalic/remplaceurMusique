#include "EncodeJob.hpp"
#include "VideoEncoder.hpp"
#include "Encode.hpp" // EncodeParams
#include <stop_token>

EncodeJob::EncodeJob(std::unique_ptr<EncodeParams> params, EncodeCallbacks callbacks)
    : m_params(std::move(params)), m_callbacks(std::move(callbacks))
{
}

EncodeJob::~EncodeJob()
{
    RequestStop();
    Join();
}

void EncodeJob::run(std::stop_token stopToken, uint64_t /*generation*/)
{
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    std::stop_callback stopCb(stopToken, [cancelFlag]() { cancelFlag->store(true); });

    if (!m_params) return;

    // Copier (plutôt que déplacer) pour qu'un Start() répété ne vide pas
    // le job et ne s'exécute pas silencieusement avec des paramètres nuls.
    auto params = std::make_unique<EncodeParams>(*m_params);
    auto callbacks = m_callbacks;

    auto encoder = VideoEncoder::Create(std::move(params), cancelFlag, std::move(callbacks));
    if (encoder) {
        encoder->Run();
    }
}