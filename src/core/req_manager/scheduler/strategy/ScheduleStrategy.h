#pragma once

#include <vector>

#include "domain/node/device.h"

class NodeManager;

struct DeviceScore {
    DeviceID id;
    Device device;
    double load;
    double weight;
    double fractional;
    int count;
};

class SchedulerStrategy {
public:
    virtual ~SchedulerStrategy() = default;

    virtual void AssignCounts(std::vector<DeviceScore> &scores,
                              int total_num,
                              size_t &rr_index) = 0;

    virtual Device SelectDevice(TaskType task_type,
                                NodeManager &node_manager,
                                size_t &rr_index) = 0;
};
