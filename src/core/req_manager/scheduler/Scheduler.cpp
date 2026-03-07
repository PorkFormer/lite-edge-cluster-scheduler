#include "core/req_manager/scheduler/Scheduler.h"

#include <spdlog/spdlog.h>

#include "core/req_manager/ReqManager.h"

SchedulerWorker::SchedulerWorker(ReqManager &manager) : manager_(manager) {}

void SchedulerWorker::Run() {
    while (!manager_.stop_.load()) {
        ClientRequest req = manager_.PopReqPending();
        if (req.req_id.empty()) {
            continue;
        }
        std::vector<SubRequest> sub_reqs;
        try {
            sub_reqs = SchedulerEngine::AllocateSubRequests(req);
        } catch (const std::exception &e) {
            spdlog::error("AllocateSubRequests failed: {}", e.what());
            continue;
        }
        manager_.repo_.UpdateRequestStatus(req.req_id, "scheduled");
        for (const auto &sub_req : sub_reqs) {
            manager_.repo_.UpsertSubRequest(sub_req, "pending");
            manager_.PushSubPending(sub_req, false);
        }
    }
}
