#pragma once

#include <string>

class ReqManager;

class ReqIngress {
public:
    explicit ReqIngress(ReqManager &manager);

    void OnChunk(void *stream_state,
                 const std::string &payload,
                 const std::string &peer) const;
    std::string Finalize(void *stream_state) const;

private:
    ReqManager &manager_;
};
