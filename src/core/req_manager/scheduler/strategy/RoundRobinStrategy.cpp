#include "core/req_manager/scheduler/strategy/RoundRobinStrategy.h"

#include <stdexcept>

#include "core/node_manager/NodeManager.h"

void RoundRobinStrategy::AssignCounts(std::vector<DeviceScore> &scores,
                                      int total_num,
                                      size_t &rr_index) {
    const int n = static_cast<int>(scores.size());
    if (n <= 0 || total_num <= 0) {
        return;
    }
    const int base = total_num / n;
    int remainder = total_num % n;
    for (auto &s : scores) {
        s.count = base;
    }
    for (int i = 0; i < remainder; ++i) {
        const int idx = static_cast<int>((rr_index + i) % scores.size());
        scores[idx].count += 1;
    }
    rr_index = (rr_index + remainder) % scores.size();
}

Device RoundRobinStrategy::SelectDevice(TaskType task_type,
                                        NodeManager &node_manager,
                                        size_t &rr_index) {
    std::shared_lock<std::shared_mutex> lock(node_manager.Mutex());
    auto &device_status = node_manager.DeviceStatusMap();
    auto &device_static_info = node_manager.DeviceStaticInfo();
    auto &device_active_services = node_manager.ActiveServices();
    if (device_status.empty()) {
        throw std::runtime_error("No available devices for scheduling.");
    }

    std::vector<DeviceID> ids;
    if (task_type != TaskType::Unknown) {
        for (const auto &pair : device_active_services) {
            const auto &device_id = pair.first;
            const auto &services = pair.second;
            if (device_status.find(device_id) == device_status.end()) {
                continue;
            }
            if (std::find(services.begin(), services.end(), task_type) != services.end()) {
                ids.push_back(device_id);
            }
        }
    }
    if (ids.empty()) {
        ids.reserve(device_status.size());
        for (const auto &pair : device_status) {
            ids.push_back(pair.first);
        }
    }

    DeviceID selected_id = ids[rr_index % ids.size()];
    rr_index = (rr_index + 1) % ids.size();
    return device_static_info[selected_id];
}
