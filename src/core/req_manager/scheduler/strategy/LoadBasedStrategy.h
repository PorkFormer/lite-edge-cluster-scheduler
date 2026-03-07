#pragma once

#include "core/req_manager/scheduler/strategy/ScheduleStrategy.h"

class LoadBasedStrategy final : public SchedulerStrategy {
public:
    void AssignCounts(std::vector<DeviceScore> &scores,
                      int total_num,
                      size_t &rr_index) override;

    Device SelectDevice(TaskType task_type,
                        NodeManager &node_manager,
                        size_t &rr_index) override;
};
