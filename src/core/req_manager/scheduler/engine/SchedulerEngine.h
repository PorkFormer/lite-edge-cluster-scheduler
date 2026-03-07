#pragma once

#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

#include "domain/node/device.h"
#include "domain/req/ClientRequest.h"
#include "domain/req/SubRequest.h"
#include "core/req_manager/scheduler/strategy/ScheduleStrategy.h"

class NodeManager;

class SchedulerEngine {
public:
    static void Init(const std::string &filepath);
    static void LoadStaticInfo(const std::string &filepath);
    static void SetNodeManager(NodeManager *manager);
    static int RegisterNode(const Device &device);
    static bool DisconnectNode(const Device &device);
    static void RemoveDevice(DeviceID global_id);

    static std::map<DeviceID, DeviceStatus> &GetDeviceStatus();
    static std::shared_mutex &GetDeviceMutex();


    static std::vector<SubRequest> AllocateSubRequests(const ClientRequest &req);
    static Device SelectDevice(TaskType task_type, ScheduleStrategy strategy);

private:
    static std::vector<DeviceID> GetCandidateDeviceIds(TaskType ttype);
    static std::vector<DeviceScore> BuildDeviceScores(const std::vector<DeviceID> &candidate_ids);

    static NodeManager *node_manager_;
    static size_t rr_index_;
};
