#include "core/req_manager/scheduler/engine/SchedulerEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include <spdlog/spdlog.h>

#include "core/node_manager/NodeManager.h"
#include "core/req_manager/scheduler/strategy/LoadBasedStrategy.h"
#include "core/req_manager/scheduler/strategy/RoundRobinStrategy.h"

namespace {
constexpr int kMaxSubReqTasks = 500;
}  // namespace

NodeManager *SchedulerEngine::node_manager_ = nullptr;
size_t SchedulerEngine::rr_index_ = 0;

void SchedulerEngine::Init(const std::string &filepath) {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }
    node_manager_->LoadStaticInfo(filepath);
}

void SchedulerEngine::LoadStaticInfo(const std::string &filepath) {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }
    node_manager_->LoadStaticInfo(filepath);
}

int SchedulerEngine::RegisterNode(const Device &device) {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }
    return node_manager_->RegisterNode(device);
}

bool SchedulerEngine::DisconnectNode(const Device &device) {
    if (!node_manager_) {
        return false;
    }
    return node_manager_->DisconnectDevice(device);
}

void SchedulerEngine::RemoveDevice(DeviceID global_id) {
    if (node_manager_) {
        node_manager_->RemoveDevice(global_id);
    }
}

std::map<DeviceID, DeviceStatus> &SchedulerEngine::GetDeviceStatus() {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }
    return node_manager_->DeviceStatusMap();
}

std::shared_mutex &SchedulerEngine::GetDeviceMutex() {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }
    return node_manager_->Mutex();
}

void SchedulerEngine::SetNodeManager(NodeManager *manager) {
    node_manager_ = manager;
}

std::vector<DeviceID> SchedulerEngine::GetCandidateDeviceIds(TaskType ttype) {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }

    std::vector<DeviceID> device_ids;
    auto &node_manager = *node_manager_;
    std::shared_lock<std::shared_mutex> lock(node_manager.Mutex());
    auto &device_active_services = node_manager.ActiveServices();
    auto &device_status = node_manager.DeviceStatusMap();
    if (ttype != TaskType::Unknown) {
        for (const auto &pair : device_active_services) {
            const auto &device_id = pair.first;
            const auto &services = pair.second;
            if (device_status.find(device_id) == device_status.end()) {
                continue;
            }
            if (std::find(services.begin(), services.end(), ttype) != services.end()) {
                device_ids.push_back(device_id);
            }
        }
    }
    if (device_ids.empty()) {
        device_ids.reserve(device_status.size());
        for (const auto &entry : device_status) {
            device_ids.push_back(entry.first);
        }
    }
    return device_ids;
}

std::vector<DeviceScore> SchedulerEngine::BuildDeviceScores(const std::vector<DeviceID> &candidate_ids) {
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }

    std::vector<DeviceScore> scores;
    auto &node_manager = *node_manager_;
    std::shared_lock<std::shared_mutex> lock(node_manager.Mutex());
    auto &device_status = node_manager.DeviceStatusMap();
    auto &device_static_info = node_manager.DeviceStaticInfo();
    scores.reserve(candidate_ids.size());
    for (const auto &device_id : candidate_ids) {
        auto status_it = device_status.find(device_id);
        auto dev_it = device_static_info.find(device_id);
        if (status_it == device_status.end() || dev_it == device_static_info.end()) {
            continue;
        }
        const auto &status = status_it->second;
        const double w_cpu = 0.3;
        const double w_mem = 0.1;
        const double w_xpu = 0.4;
        const double w_bandwidth = 1;
        const double w_net_latency = 1;
        double load = w_cpu * status.cpu_used +
                      w_mem * status.mem_used +
                      w_xpu * status.xpu_used +
                      w_bandwidth * status.net_bandwidth +
                      w_net_latency * status.net_latency;
        scores.push_back({device_id, dev_it->second, load, 0.0, 0.0, 0});
    }
    return scores;
}

std::vector<SubRequest> SchedulerEngine::AllocateSubRequests(const ClientRequest &req) {
    if (req.total_num <= 0 || req.file_names.empty()) {
        throw std::runtime_error("client request has no tasks to allocate");
    }
    if (static_cast<int>(req.file_names.size()) != req.total_num) {
        throw std::runtime_error("client request total_num does not match file list size");
    }

    auto candidate_ids = GetCandidateDeviceIds(req.task_type);
    if (candidate_ids.empty()) {
        throw std::runtime_error("no candidate devices available for batch scheduling");
    }

    auto scores = BuildDeviceScores(candidate_ids);
    if (scores.empty()) {
        throw std::runtime_error("no device status available for batch scheduling");
    }

    std::unique_ptr<SchedulerStrategy> strategy;
    if (req.schedule_strategy == ScheduleStrategy::ROUND_ROBIN) {
        strategy = std::make_unique<RoundRobinStrategy>();
    } else {
        strategy = std::make_unique<LoadBasedStrategy>();
    }
    strategy->AssignCounts(scores, req.total_num, rr_index_);

    std::vector<SubRequest> sub_reqs;
    sub_reqs.reserve(scores.size());
    size_t task_idx = 0;
    int sub_idx = 0;
    for (const auto &score : scores) {
        int remaining = score.count;
        while (remaining > 0) {
            if (task_idx >= req.file_names.size()) {
                break;
            }
            int batch = std::min(remaining, kMaxSubReqTasks);
            const int available = static_cast<int>(req.file_names.size() - task_idx);
            if (batch > available) {
                batch = available;
            }
            SubRequest sub_req;
            sub_req.req_id = req.req_id;
            sub_req.client_ip = req.client_ip;
            sub_req.task_type = req.task_type;
            sub_req.schedule_strategy = req.schedule_strategy;
            sub_req.enqueue_time_ms = req.enqueue_time_ms;
            sub_req.planned_device_id = score.id;
            sub_req.planned_device_ip = score.device.ip_address;
            sub_req.dst_device_id = boost::uuids::nil_uuid();
            sub_req.dst_device_ip.clear();
            sub_req.sub_req_id = req.req_id + "_" + std::to_string(sub_idx++);
            sub_req.start_index = static_cast<int>(task_idx);
            sub_req.sub_req_count = batch;
            task_idx += static_cast<size_t>(batch);
            if (batch <= 0) {
                break;
            }
            sub_reqs.push_back(sub_req);
            remaining -= batch;
        }
    }

    return sub_reqs;
}

Device SchedulerEngine::SelectDevice(TaskType task_type, ScheduleStrategy strategy_type) {
    std::unique_ptr<SchedulerStrategy> strategy;
    if (strategy_type == ScheduleStrategy::ROUND_ROBIN) {
        strategy = std::make_unique<RoundRobinStrategy>();
    } else {
        strategy = std::make_unique<LoadBasedStrategy>();
    }
    if (!node_manager_) {
        throw std::runtime_error("SchedulerEngine NodeManager not set");
    }
    return strategy->SelectDevice(task_type, *node_manager_, rr_index_);
}
