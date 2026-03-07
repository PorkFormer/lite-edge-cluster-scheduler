#include "core/req_manager/scheduler/strategy/LoadBasedStrategy.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "core/node_manager/NodeManager.h"

void LoadBasedStrategy::AssignCounts(std::vector<DeviceScore> &scores,
                                     int total_num,
                                     size_t &) {
    if (scores.empty() || total_num <= 0) {
        return;
    }
    const double min_load = 1e-6;
    double weight_sum = 0.0;
    for (auto &s : scores) {
        s.weight = 1.0 / std::max(s.load, min_load);
        weight_sum += s.weight;
    }
    int assigned = 0;
    for (auto &s : scores) {
        double exact = (s.weight / weight_sum) * total_num;
        s.count = static_cast<int>(std::floor(exact));
        s.fractional = exact - s.count;
        assigned += s.count;
    }
    int remainder = total_num - assigned;
    std::sort(scores.begin(), scores.end(), [](const DeviceScore &a, const DeviceScore &b) {
        return a.fractional > b.fractional;
    });
    for (int i = 0; i < remainder; ++i) {
        scores[i % scores.size()].count += 1;
    }
}

Device LoadBasedStrategy::SelectDevice(TaskType task_type,
                                       NodeManager &node_manager,
                                       size_t &) {
    std::vector<DeviceID> device_ids;
    {
        std::shared_lock<std::shared_mutex> lock(node_manager.Mutex());
        auto &device_status = node_manager.DeviceStatusMap();
        auto &device_active_services = node_manager.ActiveServices();
        if (task_type != TaskType::Unknown) {
            for (const auto &pair : device_active_services) {
                const auto &device_id = pair.first;
                const auto &services = pair.second;
                if (device_status.find(device_id) == device_status.end()) {
                    continue;
                }
                if (std::find(services.begin(), services.end(), task_type) != services.end()) {
                    device_ids.push_back(device_id);
                }
            }
        }
        if (device_ids.empty()) {
            device_ids.reserve(device_status.size());
            for (const auto &pair : device_status) {
                device_ids.push_back(pair.first);
            }
        }
    }
    if (device_ids.empty()) {
        throw std::runtime_error("No candidate devices available for scheduling.");
    }

    auto &node_manager_ref = node_manager;
    std::shared_lock<std::shared_mutex> lock(node_manager_ref.Mutex());
    auto &device_status = node_manager_ref.DeviceStatusMap();
    auto &device_static_info = node_manager_ref.DeviceStaticInfo();

    const double w_cpu = 0.3;
    const double w_mem = 0.1;
    const double w_xpu = 0.4;
    const double w_bandwidth = 1;
    const double w_net_latency = 1;

    DeviceID best_device{};
    double min_load = std::numeric_limits<double>::max();
    bool found = false;

    for (const auto &device_id : device_ids) {
        auto it = device_status.find(device_id);
        auto dev_it = device_static_info.find(device_id);
        if (it == device_status.end() || dev_it == device_static_info.end()) {
            continue;
        }
        const auto &status = it->second;
        double load = w_cpu * status.cpu_used +
                      w_mem * status.mem_used +
                      w_xpu * status.xpu_used +
                      w_bandwidth * status.net_bandwidth +
                      w_net_latency * status.net_latency;
        if (load < min_load) {
            min_load = load;
            best_device = device_id;
            found = true;
        }
    }

    if (!found) {
        throw std::runtime_error("No device statuses available for scheduling.");
    }
    return device_static_info[best_device];
}
