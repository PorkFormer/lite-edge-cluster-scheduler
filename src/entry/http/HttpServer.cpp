#include "HttpServer.h"
#include "core/req_manager/ReqManager.h"
#include "core/req_manager/scheduler/engine/SchedulerEngine.h"
#include "httplib.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <utility>
using json = nlohmann::json;
Args HttpServer::args;
NodeService *HttpServer::node_service_ = nullptr;
ReqQuery *HttpServer::req_query_ = nullptr;
ResultIngress *HttpServer::result_ingress_ = nullptr;
std::thread HttpServer::health_check_thread_;
std::atomic<bool> HttpServer::health_check_stop_{false};

namespace {
std::mutex g_result_mutex;
std::unordered_set<std::string> g_result_seen;

std::string GetFormValue(const httplib::Request &req, const std::string &name) {
    if (req.has_param(name)) {
        return req.get_param_value(name);
    }
    if (req.has_file(name)) {
        return req.get_file_value(name).content;
    }
    return "";
}

std::string ExtractSeqFromFilename(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    const size_t under = name.find_last_of('_');
    if (under == std::string::npos) {
        return "";
    }
    const size_t end = (dot == std::string::npos) ? name.size() : dot;
    if (under + 1 >= end) {
        return "";
    }
    const std::string seq = name.substr(under + 1, end - under - 1);
    if (seq.empty()) {
        return "";
    }
    for (char c : seq) {
        if (c < '0' || c > '9') {
            return "";
        }
    }
    return seq;
}
} // namespace
HttpServer::HttpServer(std::string ip, const int port, const Args &out_args,
                       NodeService &node_service,
                       ReqQuery &req_query,
                       ResultIngress &result_ingress)
    : ip(std::move(ip)), port(port) {
    args = out_args;
    node_service_ = &node_service;
    req_query_ = &req_query;
    result_ingress_ = &result_ingress;
}

bool HttpServer::Start() {
    server_ = std::make_unique<httplib::Server>();
    // �用�个工作线程
//    server_->new_task_queue = [] {
//        return new httplib::ThreadPool(64);
//    };
    // 注册��
    server_->Post(REGISTER_NODE_ROUTE, this->HandleRegisterNode);
    server_->Post(DISCONNECT_NODE_ROUTE, this->HandleDisconnect);
    server_->Get(REQ_LIST_ROUTE, this->HandleReqList);
    server_->Get(REQ_DETAIL_ROUTE, this->HandleReqDetail);
    server_->Get(SUB_REQ_ROUTE, this->HandleSubReq);
    server_->Get(NODES_ROUTE, this->HandleNodes);
    server_->Post(SCHEDULE_ROUTE, this->HandleSchedule);
    server_->Post(RECV_RST_ROUTE, this->HandleRecvResult);
    server_->Post(SUB_REQ_DONE_ROUTE, this->HandleSubReqDone);

    spdlog::info("HttpServer started success，ip:{} port:{}",this->ip, this->port);
    StartHealthCheckThread();
    auto result = server_->listen(this->ip, this->port);
    if (!result) {
        spdlog::error("HttpServer start failed!");
        return false;
    }
    return true;
}

// Define a function to modify or insert a key-value pair
void modifyOrInsert(httplib::Headers &headers,
                    const std::string &key, const std::string &newValue) {
    // Check if the key exists
    auto range = headers.equal_range(key);
    if (range.first != range.second) {
        // If found, modify the value for all matching keys
        for (auto it = range.first; it != range.second; ++it) {
            it->second = newValue;
        }
    } else {
        // If not found, insert a new key-value pair
        headers.emplace(key, newValue);
    }
}

void HttpServer::HandleRegisterNode(const httplib::Request &req, httplib::Response &res) {
    nlohmann::json jsonData = nlohmann::json::parse(req.body);
    Device device;
    device.parseJson(jsonData);
    if (node_service_) {
        node_service_->RegisterNode(device);
    }
    res.status = 200;
    res.set_content("Node registered successfully", "text/plain");
    spdlog::info("Node registered successfully, param:{}", req.body);
}

void HttpServer::HandleDisconnect(const httplib::Request &req, httplib::Response &res) {
    try {
        // 1. 解析请求 JSON
        nlohmann::json jsonData = nlohmann::json::parse(req.body);

        // 2. 解析设�信�
        Device device;
        device.parseJson(jsonData);

        // 3. 调用调度器接口删除��
        bool ok = node_service_ ? node_service_->DisconnectNode(device) : false;

        // 4. 根据删除结果返回 HTTP 响应
        if (ok) {
            res.status = 200;
            res.set_content(R"({"status":"ok","msg":"device removed success"})", "application/json");
            ReqManager::Get().RecoverTasks(device.global_id);
        } else {
            res.status = 404;
            res.set_content(R"({"status":"error","msg":"device not found"})", "application/json");
        }

    } catch (const std::exception &e) {
        spdlog::error("HandleDisconnect exception: {}", e.what());
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"invalid json or internal error"})", "application/json");
    }
}

void HttpServer::HandleReqList(const httplib::Request &req, httplib::Response &res) {
    std::string client_ip;
    if (req.has_param("client_ip")) {
        client_ip = req.get_param_value("client_ip");
    }
    json payload = req_query_ ? req_query_->GetReqList(client_ip) : json::object();
    res.status = 200;
    res.set_content(payload.dump(), "application/json");
}

void HttpServer::HandleReqDetail(const httplib::Request &req, httplib::Response &res) {
    if (!req.has_param("req_id")) {
        res.status = 400;
        res.set_content("{\"status\":\"error\",\"msg\":\"missing req_id\"}", "application/json");
        return;
    }
    std::string req_id = req.get_param_value("req_id");
    auto payload = req_query_ ? req_query_->GetReqDetail(req_id) : std::optional<json>();
    if (!payload.has_value()) {
        res.status = 404;
        res.set_content("{\"status\":\"error\",\"msg\":\"req_id not found\"}", "application/json");
        return;
    }
    res.status = 200;
    res.set_content(payload->dump(), "application/json");
}

void HttpServer::HandleSubReq(const httplib::Request &req, httplib::Response &res) {
    if (!req.has_param("sub_req_id")) {
        res.status = 400;
        res.set_content("{\"status\":\"error\",\"msg\":\"missing sub_req_id\"}", "application/json");
        return;
    }
    std::string sub_req_id = req.get_param_value("sub_req_id");
    auto payload = req_query_ ? req_query_->GetSubReqDetail(sub_req_id) : std::optional<json>();
    if (!payload.has_value()) {
        res.status = 404;
        res.set_content("{\"status\":\"error\",\"msg\":\"sub_req_id not found\"}", "application/json");
        return;
    }
    res.status = 200;
    res.set_content(payload->dump(), "application/json");
}

void HttpServer::HandleNodes(const httplib::Request &, httplib::Response &res) {
    json payload = req_query_ ? req_query_->GetNodesSnapshot() : json::object();
    res.status = 200;
    res.set_content(payload.dump(), "application/json");
}

void HttpServer::HandleSchedule(const httplib::Request &req, httplib::Response &res) {
    try {
        json payload = json::parse(req.body);
        const std::string client_ip = payload.value("ip", "");
        const std::string tasktype_str = payload.value("tasktype", "Unknown");
        const std::string req_id = payload.value("req_id", "");
        const int total_num = payload.value("total_num", 0);

        if (client_ip.empty()) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"ip required"})", "application/json");
            return;
        }
        if (req_id.empty()) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"req_id required"})", "application/json");
            return;
        }

        ScheduleStrategy strategy = ScheduleStrategy::LOAD_BASED;
        if (req.has_param("stargety")) {
            const std::string st = req.get_param_value("stargety");
            if (st == "roundrobin") {
                strategy = ScheduleStrategy::ROUND_ROBIN;
            }
        }

        ClientRequest creq;
        creq.req_id = req_id;
        creq.client_ip = client_ip;
        creq.task_type = StrToTaskType(tasktype_str);
        creq.schedule_strategy = strategy;
        creq.enqueue_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();

        std::vector<std::string> filenames;
        if (payload.contains("filenames") && payload["filenames"].is_array()) {
            for (const auto &item : payload["filenames"]) {
                if (item.is_string()) {
                    filenames.push_back(item.get<std::string>());
                }
            }
        } else if (payload.contains("filename") && payload["filename"].is_string()) {
            filenames.push_back(payload["filename"].get<std::string>());
        }

        if (!payload.contains("req_dir") || !payload["req_dir"].is_string()) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"req_dir required"})", "application/json");
            return;
        }
        std::filesystem::path base = payload["req_dir"].get<std::string>();

        std::vector<std::string> file_names;
        if (!filenames.empty()) {
            file_names = filenames;
        } else {
            if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base)) {
                res.status = 404;
                res.set_content(R"({"status":"error","msg":"req dir not found"})", "application/json");
                return;
            }
            for (const auto &entry : std::filesystem::directory_iterator(base)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                file_names.push_back(entry.path().filename().string());
            }
            std::sort(file_names.begin(), file_names.end());
        }

        if (file_names.empty()) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"no files found"})", "application/json");
            return;
        }
        if (total_num > 0 && static_cast<int>(file_names.size()) != total_num) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"total_num mismatch"})", "application/json");
            return;
        }

        creq.total_num = total_num > 0 ? total_num : static_cast<int>(file_names.size());
        creq.req_dir = base.string();
        creq.file_names = std::move(file_names);

        ReqManager::Get().EnqueueRequest(creq);

        res.status = 200;
        res.set_content(R"({"status":"queued","msg":"task enqueued"})", "application/json");
    } catch (const std::exception &e) {
        spdlog::error("HandleSchedule exception: {}", e.what());
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"invalid json"})", "application/json");
    }
}

void HttpServer::HandleRecvResult(const httplib::Request &req, httplib::Response &res) {
    if (!req.is_multipart_form_data()) {
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"expect multipart/form-data"})", "application/json");
        return;
    }
    if (!req.has_file("file")) {
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"missing file"})", "application/json");
        return;
    }

    const auto file = req.get_file_value("file");
    std::string service = GetFormValue(req, "service");
    std::string client_ip = GetFormValue(req, "client_ip");
    std::string client_port_str = GetFormValue(req, "client_port");
    std::string req_id = GetFormValue(req, "req_id");
    std::string task_id = GetFormValue(req, "task_id");
    std::string sub_req_id = GetFormValue(req, "sub_req_id");
    if (sub_req_id.empty()) {
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"sub_req_id required"})", "application/json");
        return;
    }
    if (task_id.empty()) {
        task_id = file.filename;
    }
    std::string seq = GetFormValue(req, "seq");
    if (seq.empty()) {
        seq = ExtractSeqFromFilename(task_id);
    }

    if (client_ip.empty()) {
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"client_ip required"})", "application/json");
        return;
    }

    int client_port = 8889;
    if (!client_port_str.empty()) {
        try {
            client_port = std::stoi(client_port_str);
        } catch (...) {
            client_port = 8889;
        }
    }

    std::string key;
    if (!req_id.empty() && !seq.empty()) {
        key = req_id + ":" + seq;
    } else if (!req_id.empty()) {
        key = req_id + ":" + task_id;
    } else {
        key = client_ip + ":" + task_id;
    }
    {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        if (g_result_seen.find(key) != g_result_seen.end()) {
            res.status = 200;
            res.set_content(R"({"status":"ok","msg":"duplicate"})", "application/json");
            return;
        }
    }

    std::string tasktype = result_ingress_ ? result_ingress_->ResolveTaskTypeForSubReq(sub_req_id) : "";
    if (tasktype.empty()) {
        tasktype = service.empty() ? "Unknown" : service;
    }
    std::string saved_path = result_ingress_ ? result_ingress_->SaveResultFile(client_ip, tasktype, req_id, sub_req_id, file.filename, file.content) : "";
    if (saved_path.empty()) {
        res.status = 500;
        res.set_content(R"({"status":"error","msg":"save failed"})", "application/json");
        return;
    }

        if (result_ingress_) {
    result_ingress_->EnqueueResult(client_ip,
                                    client_port,
                                    service,
                                    file.filename,
                                    saved_path,
                                    file.content_type,
                                    req_id,
                                    sub_req_id,
                                    task_id,
                                    seq);
        }
    {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        g_result_seen.insert(key);
    }

    res.status = 200;
    res.set_content(R"({"status":"queued"})", "application/json");
}

void HttpServer::HandleSubReqDone(const httplib::Request &req, httplib::Response &res) {
    try {
        json payload = json::parse(req.body);
        const std::string sub_req_id = payload.value("sub_req_id", "");
        const std::string status = payload.value("status", "success");
        if (sub_req_id.empty()) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"sub_req_id required"})", "application/json");
            return;
        }

        if (status == "success") {
            if (result_ingress_) {
                result_ingress_->MarkSubReqResultsReady(sub_req_id);
            }
        } else {
            ReqManager::Get().CompleteSubReq(sub_req_id, status);
        }

        res.status = 200;
        res.set_content(R"({"status":"ok"})", "application/json");
    } catch (const std::exception &e) {
        spdlog::error("HandleSubReqDone exception: {}", e.what());
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"invalid json"})", "application/json");
    }
}

void HttpServer::StartHealthCheckThread() {
    // Start only once; the server runs for the lifetime of the process.
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) {
        return;
    }

    health_check_stop_.store(false);
    health_check_thread_ = std::thread(&HttpServer::HealthCheckLoop, this);
    health_check_thread_.detach();
    spdlog::info("Gateway health-check thread started (interval_ms={}, latency_threshold_sec={})",
                 HEALTH_CHECK_INTERVAL, HEALTH_CHECK_LATENCY_THRESHOLD);
}

void HttpServer::HealthCheckLoop() {
    using Clock = std::chrono::steady_clock;

    struct UuidHasher {
        size_t operator()(const DeviceID &id) const noexcept {
            const uint8_t *p = id.data;
            size_t h = 1469598103934665603ull;
            for (size_t i = 0; i < 16; ++i) {
                h ^= static_cast<size_t>(p[i]);
                h *= 1099511628211ull;
            }
            return h;
        }
    };
    std::unordered_map<DeviceID, Clock::time_point, UuidHasher> last_recover;

    while (!health_check_stop_.load()) {
        std::vector<DeviceID> to_recover;
        {
            std::shared_lock<std::shared_mutex> lock(SchedulerEngine::GetDeviceMutex());
            auto &device_status = SchedulerEngine::GetDeviceStatus();
            const auto now = Clock::now();

            for (const auto &[dev_id, status] : device_status) {
                const double latency_sec = status.net_latency / 1000.0; // agent reports ms
                if (latency_sec <= HEALTH_CHECK_LATENCY_THRESHOLD) {
                    continue;
                }

                const auto it = last_recover.find(dev_id);
                if (it != last_recover.end()) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                    if (elapsed < HEALTH_CHECK_COOLDOWN_SEC) {
                        continue;
                    }
                }

                last_recover[dev_id] = now;
                to_recover.push_back(dev_id);
            }
        }

        for (const auto &dev_id : to_recover) {
            spdlog::warn("HealthCheck: latency > {}s, trigger service migration (recover running tasks), device_id={}",
                         HEALTH_CHECK_LATENCY_THRESHOLD, boost::uuids::to_string(dev_id));
            ReqManager::Get().RecoverTasks(dev_id);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(HEALTH_CHECK_INTERVAL));
    }
}

void HttpServer::Stop() {
    health_check_stop_.store(true);
    if (server_) {
        server_->stop();
    }
}
