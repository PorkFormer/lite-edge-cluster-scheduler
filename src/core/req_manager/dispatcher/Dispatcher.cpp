#include "core/req_manager/dispatcher/Dispatcher.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "core/req_manager/ReqManager.h"

namespace {
constexpr int kMaxRetries = 3;
constexpr int kMaxInFlightSubReqsPerDevice = 10;
}  // namespace

DispatcherWorker::DispatcherWorker(ReqManager &manager) : manager_(manager) {}

void DispatcherWorker::Run() {
    while (!manager_.stop_.load()) {
        SubRequest sub_req;
        if (!manager_.PopSubPending(sub_req)) {
            continue;
        }

        auto &node_manager = manager_.GetNodeManager();
        auto &device_static_info = node_manager.DeviceStaticInfo();
        auto &device_status = node_manager.DeviceStatusMap();

        Device target_device;
        bool use_assigned = (sub_req.dst_device_id != boost::uuids::nil_uuid());
        bool use_planned = (!use_assigned && sub_req.planned_device_id != boost::uuids::nil_uuid());
        try {
            bool selected = false;
            if (use_assigned) {
                auto it = device_static_info.find(sub_req.dst_device_id);
                if (it == device_static_info.end()) {
                    throw std::runtime_error("assigned device not found");
                }
                target_device = it->second;
                selected = true;
            }
            if (!selected && use_planned) {
                auto it = device_static_info.find(sub_req.planned_device_id);
                auto status_it = device_status.find(sub_req.planned_device_id);
                if (it != device_static_info.end() && status_it != device_status.end()) {
                    target_device = it->second;
                    selected = true;
                }
            }
            if (!selected) {
                target_device = sub_req.schedule_strategy == ScheduleStrategy::ROUND_ROBIN
                                    ? SchedulerEngine::SelectDevice(sub_req.task_type, ScheduleStrategy::ROUND_ROBIN)
                                    : SchedulerEngine::SelectDevice(sub_req.task_type, ScheduleStrategy::LOAD_BASED);
            }
        } catch (const std::exception &e) {
            spdlog::warn("Select device failed for sub_req {}: {}", sub_req.sub_req_id, e.what());
            SubRequest retry_sub = manager_.BuildRetrySubRequest(sub_req, "select_device_failed");
            if (retry_sub.attempt <= 3) {
                manager_.PushSubPending(retry_sub, true);
            }
            continue;
        }

        if (manager_.CountInFlightSubReqs(target_device.global_id) >= kMaxInFlightSubReqsPerDevice) {
            spdlog::warn("Device {} in-flight limit reached, requeue sub_req {}",
                         target_device.ip_address, sub_req.sub_req_id);
            manager_.PushSubPending(sub_req, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        sub_req.dst_device_id = target_device.global_id;
        sub_req.dst_device_ip = target_device.ip_address;

        std::string req_dir;
        std::vector<std::string> file_names;
        if (!manager_.repo_.GetRequestFileSlice(sub_req.req_id,
                                                sub_req.start_index,
                                                sub_req.sub_req_count,
                                                &req_dir,
                                                &file_names)) {
            spdlog::warn("Request files missing for sub_req {}", sub_req.sub_req_id);
            SubRequest retry_sub = manager_.BuildRetrySubRequest(sub_req, "req_files_missing");
            if (retry_sub.attempt <= kMaxRetries) {
                manager_.PushSubPending(retry_sub, true);
            }
            continue;
        }

        std::vector<std::string> file_paths;
        file_paths.reserve(file_names.size());
        for (const auto &name : file_names) {
            file_paths.push_back((std::filesystem::path(req_dir) / name).string());
        }

        try {
            httplib::Client meta_cli(target_device.ip_address, 20810);
            nlohmann::json meta_payload;
            meta_payload["req_id"] = sub_req.req_id;
            meta_payload["sub_req_id"] = sub_req.sub_req_id;
            meta_payload["sub_req_count"] = sub_req.sub_req_count;
            meta_payload["tasktype"] = sub_req.task_type == TaskType::Unknown ? "Unknown" : nlohmann::json(sub_req.task_type);
            meta_payload["dst_device_id"] = boost::uuids::to_string(sub_req.dst_device_id);
            meta_payload["dst_device_ip"] = target_device.ip_address;
            meta_payload["enqueue_time_ms"] = sub_req.enqueue_time_ms;
            auto res = meta_cli.Post("/recv_sub_req_meta", meta_payload.dump(), "application/json");
            if (!res || res->status != 200) {
                spdlog::warn("Send sub_req_meta {} failed, status={}", sub_req.sub_req_id, res ? res->status : -1);
                SubRequest retry_sub = manager_.BuildRetrySubRequest(sub_req, "meta_failed");
                if (retry_sub.attempt <= kMaxRetries) {
                    manager_.PushSubPending(retry_sub, true);
                }
                continue;
            }
        } catch (const std::exception &e) {
            spdlog::error("Exception sending sub_req_meta {}: {}", sub_req.sub_req_id, e.what());
            SubRequest retry_sub = manager_.BuildRetrySubRequest(sub_req, "meta_exception");
            if (retry_sub.attempt <= kMaxRetries) {
                manager_.PushSubPending(retry_sub, true);
            }
            continue;
        }

        bool all_sent = true;
        for (size_t i = 0; i < file_names.size(); ++i) {
            const std::string &file_name = file_names[i];
            const std::string &file_path = file_paths[i];
            std::ifstream ifs(file_path, std::ios::binary);
            if (!ifs) {
                spdlog::error("Failed to open task file: {}", file_path);
                all_sent = false;
                break;
            }
            std::ostringstream buffer;
            buffer << ifs.rdbuf();
            std::string image_data = buffer.str();

            nlohmann::json meta_json;
            meta_json["ip"] = sub_req.client_ip;
            meta_json["file_name"] = file_name;
            meta_json["tasktype"] = sub_req.task_type == TaskType::Unknown ? "Unknown" : nlohmann::json(sub_req.task_type);
            meta_json["req_id"] = sub_req.req_id;
            meta_json["sub_req_id"] = sub_req.sub_req_id;
            meta_json["sub_req_count"] = sub_req.sub_req_count;
            std::string meta_str = meta_json.dump();

            try {
                httplib::Client cli(target_device.ip_address, 20810);
                httplib::MultipartFormDataItems form_items = {
                    {"pic_file", image_data, file_name, "application/octet-stream"},
                    {"pic_info", meta_str, "", "application/json"}
                };
                auto res = cli.Post("/recv_task", form_items);
                if (!res || res->status != 200) {
                    spdlog::warn("Send task {} failed, status={}", file_name, res ? res->status : -1);
                    all_sent = false;
                    break;
                }
            } catch (const std::exception &e) {
                spdlog::error("Exception sending task {}: {}", file_name, e.what());
                all_sent = false;
                break;
            }
        }

        if (!all_sent) {
            SubRequest retry_sub = manager_.BuildRetrySubRequest(sub_req, "send_failed");
            if (retry_sub.attempt <= kMaxRetries) {
                manager_.PushSubPending(retry_sub, true);
            }
            continue;
        }

        manager_.AddRunningSubReq(target_device.global_id, sub_req);
        manager_.repo_.UpdateSubRequestDispatched(sub_req.sub_req_id, target_device);
        spdlog::info("SubReq {} dispatched to device {} ({} tasks)",
                     sub_req.sub_req_id, target_device.ip_address, sub_req.sub_req_count);
    }
}
