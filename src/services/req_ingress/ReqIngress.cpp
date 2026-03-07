#include "services/req_ingress/ReqIngress.h"

#include "core/req_manager/ReqManager.h"

ReqIngress::ReqIngress(ReqManager &manager) : manager_(manager) {}

void ReqIngress::OnChunk(void *stream_state,
                         const std::string &payload,
                         const std::string &peer) const {
    if (!stream_state) {
        return;
    }
    auto *state = static_cast<ReqManager::StreamState *>(stream_state);
    manager_.HandleUploadStreamChunk(*state, payload, peer);
}

std::string ReqIngress::Finalize(void *stream_state) const {
    if (!stream_state) {
        return "{}";
    }
    auto *state = static_cast<ReqManager::StreamState *>(stream_state);
    return manager_.FinalizeUploadStream(*state);
}
