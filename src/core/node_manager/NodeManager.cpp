#include "NodeManager.h"

#include <fstream>
#include <thread>
#include <chrono>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <boost/uuid/uuid_io.hpp>

using json = nlohmann::json;

int NodeManager::RegisterNode(const Device &device) {
    device_static_info_[device.global_id] = device;
    device_status_[device.global_id] = DeviceStatus();
    device_active_services_[device.global_id] = device.services;
    return 0;
}

void NodeManager::RemoveDevice(DeviceID global_id) {
    device_active_services_.erase(global_id);
}

void NodeManager::DisplayDevices() {
    for (auto [id, status] : device_status_) {
        if (device_static_info_.count(id) > 0) {
            spdlog::info("Device Type: {}", device_static_info_[id].type);
            spdlog::info("Device Status: ");
            status.show();
        }
    }
}

void NodeManager::DisplayDeviceInfo() {
    for (const auto &[device_id, device] : device_static_info_) {
        spdlog::info("Device {} type {}", boost::uuids::to_string(device_id), device.type);
    }
}

void NodeManager::LoadStaticInfo(const std::string &filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file");
    }
    json j;
    file >> j;
    for (auto &task : j.items()) {
        TaskType taskType;
        from_json(task.key(), taskType);
        for (auto &device : task.value().items()) {
            DeviceType deviceType;
            from_json(device.key(), deviceType);
            static_info_[taskType][deviceType] = StaticInfoItem(device.value());
        }
    }
}

std::map<TaskType, std::map<DeviceType, StaticInfoItem>> NodeManager::GetStaticInfo() const {
    return static_info_;
}

void NodeManager::StartDeviceInfoCollection() {
    std::thread([this]() {
        int count = 0;
        while (true) {
            {
                std::unique_lock<std::shared_mutex> lock(devs_mutex_);
                for (auto [k, dev] : device_static_info_) {
                    httplib::Client cli(dev.ip_address, dev.agent_port);
                    httplib::Result res;
                    try {
                        res = cli.Get("/usage/device_info");
                        if (res != nullptr && res.error() == httplib::Error::Success) {
                            std::string restr = res->body.data();
                            json j = json::parse(restr);
                            std::string resp_status = j["status"];
                            if (resp_status != "success") {
                                spdlog::error("Failed to get device info, agent return filed,dev.ip_address:{}, dev.agent_port:{}",
                                              dev.ip_address, dev.agent_port);
                                continue;
                            }
                            DeviceStatus status;
                            status.from_json(j["result"]);

                            auto it = device_status_.find(k);
                            if (it != device_status_.end()) {
                                it->second = status;
                            }

                            try {
                                if (j.contains("result") && j["result"].is_object() &&
                                    j["result"].contains("services") && j["result"]["services"].is_array()) {
                                    std::vector<TaskType> running;
                                    for (const auto &sv : j["result"]["services"]) {
                                        if (!sv.is_string()) continue;
                                        TaskType tt = StrToTaskType(sv.get<std::string>());
                                        if (tt != TaskType::Unknown) {
                                            running.push_back(tt);
                                        }
                                    }
                                    device_active_services_[k] = running;
                                }
                            } catch (...) {
                            }
                        } else {
                            spdlog::error("Failed to get device info, dev.ip_address:{}, dev.agent_port:{}",
                                          dev.ip_address, dev.agent_port);
                            continue;
                        }
                    } catch (const std::exception &e) {
                        spdlog::error("collect info error: {}", e.what());
                        continue;
                    }
                }
            }

            if (++count % 10 == 0) {
                spdlog::info("=== Device Load Summary ===");
                for (const auto &[device_id, dev] : device_static_info_) {
                    auto status_it = device_status_.find(device_id);
                    if (status_it != device_status_.end()) {
                        const auto &status = status_it->second;
                        spdlog::info("Device {} ({}): CPU: {:.2f}%. MEM: {:.2f}%, XPU: {:.2f}%, Bandwidth: {:.2f}Mbps, Latency: {}ms",
                                     dev.ip_address, dev.type,
                                     status.cpu_used * 100, status.mem_used * 100, status.xpu_used * 100,
                                     status.net_bandwidth, static_cast<int>(status.net_latency * 1000));
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }).detach();
}

bool NodeManager::DisconnectDevice(const Device &device) {
    try {
        bool removed = false;
        std::unique_lock<std::shared_mutex> lock(devs_mutex_);
        auto it = device_status_.find(device.global_id);
        if (it != device_status_.end()) {
            device_status_.erase(it);
            removed = true;
        } else {
            spdlog::warn("Device {} not found in device_status.", boost::uuids::to_string(device.global_id));
            return false;
        }
        lock.unlock();
        if (removed) {
            spdlog::info("Device {} disconnected and removed.", boost::uuids::to_string(device.global_id));
            return true;
        }
    } catch (const std::exception &e) {
        spdlog::error("DisconnectDevice exception: {}", e.what());
        return false;
    }
    return false;
}
