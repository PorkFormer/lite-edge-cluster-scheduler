#include "services/node_service/NodeService.h"

#include "core/req_manager/scheduler/engine/SchedulerEngine.h"

int NodeService::RegisterNode(const Device &device) const {
    return SchedulerEngine::RegisterNode(device);
}

bool NodeService::DisconnectNode(const Device &device) const {
    return SchedulerEngine::DisconnectNode(device);
}
