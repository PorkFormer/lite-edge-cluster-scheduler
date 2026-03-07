
#include "ReqManager.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <unordered_set>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace {
constexpr const char *kDefaultTaskType = "yolo";
constexpr int kMaxRetries = 3;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string TaskTypeToString(TaskType t) {
    return t == TaskType::Unknown ? "Unknown" : to_string(nlohmann::json(t));
}

std::string SafeName(const std::string &name) {
    std::string safe = name;
    if (safe.empty()) {
        safe = kDefaultTaskType;
    }
    std::filesystem::path p(safe);
    safe = p.filename().string();
    for (char &ch : safe) {
        if (ch == '/' || ch == '\\') {
            ch = '_';
        }
    }
    if (safe.empty()) {
        safe = kDefaultTaskType;
    }
    return safe;
}

std::string NormalizeClientIp(const std::string &peer) {
    if (peer.rfind("ipv4:", 0) == 0) {
        std::string rest = peer.substr(5);
        auto pos = rest.find(':');
        return pos == std::string::npos ? rest : rest.substr(0, pos);
    }
    if (peer.rfind("ipv6:", 0) == 0) {
        std::string rest = peer.substr(5);
        if (!rest.empty() && rest[0] == '[') {
            auto pos = rest.find(']');
            if (pos != std::string::npos) {
                return rest.substr(1, pos - 1);
            }
        }
        auto pos = rest.find(':');
        return pos == std::string::npos ? rest : rest.substr(0, pos);
    }
    return "unknown";
}

bool DecodeBase64(const std::string &input, std::string &output) {
    static const int8_t kDecTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,64,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    output.clear();
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            continue;
        }
        int8_t d = kDecTable[c];
        if (d == -1) {
            return false;
        }
        if (d == 64) {
            break;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return true;
}

std::unordered_map<std::string, int> LoadExistingSequences(const std::filesystem::path &upload_root) {
    std::unordered_map<std::string, int> sequences;
    if (!std::filesystem::exists(upload_root)) {
        return sequences;
    }
    std::regex name_pattern(R"(^([0-9a-fA-F:\.]+)_(\d+)(\.[A-Za-z0-9]+)?$)");
    for (const auto &task_entry : std::filesystem::directory_iterator(upload_root)) {
        if (!task_entry.is_directory()) {
            continue;
        }
        const auto &task_dir = task_entry.path();
        for (const auto &ip_entry : std::filesystem::directory_iterator(task_dir)) {
            if (!ip_entry.is_directory()) {
                continue;
            }
            const auto &ip_dir = ip_entry.path();
            for (const auto &req_entry : std::filesystem::directory_iterator(ip_dir)) {
                if (!req_entry.is_directory()) {
                    continue;
                }
                const auto &req_dir = req_entry.path();
                int max_seq = 0;
                for (const auto &file_entry : std::filesystem::directory_iterator(req_dir)) {
                    if (!file_entry.is_regular_file()) {
                        continue;
                    }
                    const std::string fname = file_entry.path().filename().string();
                    std::smatch match;
                    if (!std::regex_match(fname, match, name_pattern)) {
                        continue;
                    }
                    const std::string ip = match.str(1);
                    if (ip != ip_dir.filename().string()) {
                        continue;
                    }
                    int seq = std::stoi(match.str(2));
                    if (seq > max_seq) {
                        max_seq = seq;
                    }
                }
                std::string key = task_dir.filename().string() + "/" + ip_dir.filename().string() + "/" + req_dir.filename().string();
                sequences[key] = max_seq;
            }
        }
    }
    return sequences;
}

}

ReqManager::ReqManager(const Args &args, NodeManager &node_manager)
    : upload_root_(args.grpc_upload_root.empty()
                      ? (std::filesystem::current_path() / "workspace" / "master" / "Input").string()
                      : args.grpc_upload_root),
      output_root_((std::filesystem::current_path() / "workspace" / "master" / "output").string()),
      strategy_(args.grpc_strategy.empty() ? "load" : args.grpc_strategy),
      db_path_(args.db_path.empty()
                   ? (std::filesystem::current_path() / "workspace" / "master" / "data" / "req_manager.db").string()
                   : args.db_path),
      repo_(db_path_),
      node_manager_(&node_manager),
      scheduler_worker_(*this),
      dispatcher_worker_(*this),
      result_sender_worker_(*this) {
    std::filesystem::create_directories(upload_root_);
    std::filesystem::create_directories(output_root_);
    seq_map_ = LoadExistingSequences(upload_root_);
    if (!repo_.Init()) {
        spdlog::warn("ReqRepository init failed, persistence disabled");
    }
}

void ReqManager::SetInstance(ReqManager *instance) {
    instance_ = instance;
}

ReqManager &ReqManager::Get() {
    return *instance_;
}

ReqManager *ReqManager::instance_ = nullptr;

NodeManager &ReqManager::GetNodeManager() {
    return *node_manager_;
}

const NodeManager &ReqManager::GetNodeManager() const {
    return *node_manager_;
}

void ReqManager::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    StartWorkers();
}

void ReqManager::Stop() {
    // Prevent repeated Stop calls
    if (stop_.exchange(true)) {
        return;
    }
    req_cv_.notify_all();
    sub_cv_.notify_all();
    rst_cv_.notify_all();
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
    if (dispatcher_thread_.joinable()) {
        dispatcher_thread_.join();
    }
    if (result_sender_thread_.joinable()) {
        result_sender_thread_.join();
    }
}

void ReqManager::StartWorkers() {
    scheduler_thread_ = std::thread([this]() { scheduler_worker_.Run(); });
    dispatcher_thread_ = std::thread([this]() { dispatcher_worker_.Run(); });
    result_sender_thread_ = std::thread([this]() { result_sender_worker_.Run(); });
}

void ReqManager::EnqueueRequest(const ClientRequest &req) {
    repo_.UpsertRequest(req);
    repo_.ReplaceRequestFiles(req.req_id, req.file_names);
    PushReqPending(req);
}

void ReqManager::EnqueueResult(std::string client_ip,
                               int client_port,
                               std::string service,
                               std::string file_name,
                               std::string file_path,
                               std::string content_type,
                               std::string req_id,
                               std::string sub_req_id,
                               std::string task_id,
                               std::string seq) {
    if (sub_req_id.empty()) {
        spdlog::warn("EnqueueResult skipped: missing sub_req_id (file_name={})", file_name);
        return;
    }
    ResultItem item;
    bool ready_to_send = false;
    {
        std::lock_guard<std::mutex> lock(rst_state_mutex_);
        SubReqResultState &state = rst_states_[sub_req_id];
        if (state.sub_req_id.empty()) {
            state.sub_req_id = sub_req_id;
        }
        if (state.client_ip.empty()) {
            state.client_ip = client_ip;
        }
        if (state.client_port == 0) {
            state.client_port = client_port;
        }
        if (state.service.empty()) {
            state.service = service;
        }
        if (state.req_id.empty()) {
            state.req_id = req_id;
        }
        if (state.tasktype.empty()) {
            std::string tasktype;
            std::string req_id_from_sub;
            std::string client_ip_from_sub;
            int expected = 0;
            if (GetSubReqMeta(sub_req_id, &tasktype, &req_id_from_sub, &client_ip_from_sub, &expected)) {
                if (state.tasktype.empty()) {
                    state.tasktype = tasktype;
                }
                if (state.req_id.empty()) {
                    state.req_id = req_id_from_sub;
                }
                if (state.client_ip.empty()) {
                    state.client_ip = client_ip_from_sub;
                }
                if (state.expected_count == 0) {
                    state.expected_count = expected;
                }
            }
        }

        state.file_names.push_back(file_name);
        state.file_paths.push_back(file_path);
        state.content_types.push_back(content_type);

        ready_to_send = TryBuildResultItemLocked(state, item);
        if (ready_to_send) {
            rst_states_.erase(sub_req_id);
        }
    }
    if (ready_to_send) {
        {
            std::lock_guard<std::mutex> lock(rst_mutex_);
            rst_queue_.push_back(std::move(item));
        }
        rst_cv_.notify_one();
    }
}

void ReqManager::CompleteSubReq(const std::string &sub_req_id, const std::string &status) {
    if (status == "success") {
        repo_.MarkSubRequestCompleted(sub_req_id);
    } else {
        repo_.MarkSubRequestFailed(sub_req_id, status);
    }

    std::string req_id;
    std::lock_guard<std::mutex> lock(sub_mutex_);
    for (auto &entry : sub_running_) {
        auto &sub_reqs = entry.second;
        for (auto it = sub_reqs.begin(); it != sub_reqs.end();) {
            if (*it == sub_req_id) {
                it = sub_reqs.erase(it);
            } else {
                ++it;
            }
        }
    }
    auto it = sub_req_store_.find(sub_req_id);
    if (it != sub_req_store_.end()) {
        req_id = it->second.req_id;
        sub_req_store_.erase(it);
    }

    if (status == "success" && !req_id.empty()) {
        std::vector<SubReqRecord> subs;
        if (repo_.ListSubRequestsByReq(req_id, &subs)) {
            bool all_completed = !subs.empty();
            bool any_failed = false;
            for (const auto &rec : subs) {
                if (rec.status == "failed") {
                    any_failed = true;
                    all_completed = false;
                    break;
                }
                if (rec.status != "completed") {
                    all_completed = false;
                }
            }
            if (all_completed && !any_failed) {
                repo_.UpdateRequestStatus(req_id, "completed");
            }
        }
    }
}

void ReqManager::MarkSubReqResultsReady(const std::string &sub_req_id) {
    ResultItem item;
    bool ready_to_send = false;
    {
        std::lock_guard<std::mutex> lock(rst_state_mutex_);
        SubReqResultState &state = rst_states_[sub_req_id];
        if (state.sub_req_id.empty()) {
            state.sub_req_id = sub_req_id;
        }
        if (state.expected_count == 0) {
            std::string tasktype;
            std::string req_id;
            std::string client_ip;
            int expected = 0;
            if (GetSubReqMeta(sub_req_id, &tasktype, &req_id, &client_ip, &expected)) {
                if (state.tasktype.empty()) {
                    state.tasktype = tasktype;
                }
                if (state.req_id.empty()) {
                    state.req_id = req_id;
                }
                if (state.client_ip.empty()) {
                    state.client_ip = client_ip;
                }
                if (state.expected_count == 0) {
                    state.expected_count = expected;
                }
            }
        }
        state.ready_signal = true;
        ready_to_send = TryBuildResultItemLocked(state, item);
        if (ready_to_send) {
            rst_states_.erase(sub_req_id);
        }
    }
    if (ready_to_send) {
        {
            std::lock_guard<std::mutex> lock(rst_mutex_);
            rst_queue_.push_back(std::move(item));
        }
        rst_cv_.notify_one();
    }
}

bool ReqManager::GetSubReqMeta(const std::string &sub_req_id,
                               std::string *out_tasktype,
                               std::string *out_req_id,
                               std::string *out_client_ip,
                               int *out_expected_count) const {
    {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        auto it = sub_req_store_.find(sub_req_id);
        if (it != sub_req_store_.end()) {
            if (out_tasktype) {
                *out_tasktype = TaskTypeToString(it->second.task_type);
            }
            if (out_req_id) {
                *out_req_id = it->second.req_id;
            }
            if (out_client_ip) {
                *out_client_ip = it->second.client_ip;
            }
            if (out_expected_count) {
                *out_expected_count = it->second.sub_req_count;
            }
            return true;
        }
    }
    SubReqRecord rec;
    if (!repo_.GetSubRequest(sub_req_id, &rec)) {
        return false;
    }
    if (out_tasktype) {
        *out_tasktype = TaskTypeToString(rec.sub_req.task_type);
    }
    if (out_req_id) {
        *out_req_id = rec.sub_req.req_id;
    }
    if (out_client_ip) {
        *out_client_ip = rec.sub_req.client_ip;
    }
    if (out_expected_count) {
        *out_expected_count = rec.sub_req.sub_req_count;
    }
    return true;
}

bool ReqManager::TryBuildResultItemLocked(SubReqResultState &state, ResultItem &out) {
    if (!state.ready_signal) {
        return false;
    }
    if (state.expected_count <= 0 || static_cast<int>(state.file_paths.size()) < state.expected_count) {
        return false;
    }

    out.client_ip = state.client_ip;
    out.client_port = state.client_port;
    out.service = state.service;
    out.req_id = state.req_id;
    out.sub_req_id = state.sub_req_id;
    out.tasktype = state.tasktype;
    out.file_names = std::move(state.file_names);
    out.file_paths = std::move(state.file_paths);
    out.content_types = std::move(state.content_types);

    repo_.MarkSubRequestRstSending(out.sub_req_id);
    return true;
}

void ReqManager::RecoverTasks(const DeviceID &device_id) {
    std::vector<SubRequest> to_retry;
    {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        auto it = sub_running_.find(device_id);
        if (it == sub_running_.end()) {
            return;
        }
        auto &sub_reqs = it->second;
        for (const auto &sub_req_id : sub_reqs) {
            auto sub_it = sub_req_store_.find(sub_req_id);
            if (sub_it == sub_req_store_.end()) {
                continue;
            }
            to_retry.push_back(sub_it->second);
        }
        sub_running_.erase(it);
    }
    for (const auto &sub_req : to_retry) {
        SubRequest retry_sub = BuildRetrySubRequest(sub_req, "device_lost");
        if (retry_sub.attempt <= kMaxRetries) {
            PushSubPending(retry_sub, true);
        }
    }
}

std::vector<std::string> ReqManager::GetPendingSubReqIds() const {
    std::lock_guard<std::mutex> lock(sub_mutex_);
    std::vector<std::string> out;
    out.reserve(sub_pending_queue_.size());
    for (const auto &id : sub_pending_queue_) {
        out.push_back(id);
    }
    return out;
}

nlohmann::json ReqManager::BuildReqList(const std::string &client_ip) const {
    json out;
    std::map<std::string, json> grouped;
    std::vector<ReqRecord> reqs;
    if (!repo_.ListRequests(client_ip, &reqs)) {
        out["clients"] = json::array();
        return out;
    }

    for (const auto &rec : reqs) {
        const auto &req = rec.req;
        json req_json;
        req_json["req_id"] = req.req_id;
        req_json["client_ip"] = req.client_ip;
        req_json["tasktype"] = TaskTypeToString(req.task_type);
        req_json["total"] = req.total_num;
        int sent_count = 0;
        int running_count = 0;
        int waiting_count = 0;
        bool all_completed = true;
        bool any_failed = false;
        json sub_id_arr = json::array();

        std::vector<SubReqRecord> subs;
        if (repo_.ListSubRequestsByReq(req.req_id, &subs)) {
            all_completed = !subs.empty();
            for (const auto &sub_rec : subs) {
                sub_id_arr.push_back(sub_rec.sub_req.sub_req_id);
                const int task_total = sub_rec.sub_req.sub_req_count;
                if (sub_rec.status == "failed") {
                    any_failed = true;
                    all_completed = false;
                } else if (sub_rec.status == "completed") {
                    sent_count += task_total;
                } else if (sub_rec.status == "rst_sending" || sub_rec.status == "running") {
                    running_count += task_total;
                    all_completed = false;
                } else {
                    waiting_count += task_total;
                    all_completed = false;
                }
            }
        } else {
            all_completed = false;
        }

        std::string req_status;
        if (any_failed) {
            req_status = "failed";
        } else if (all_completed && !sub_id_arr.empty()) {
            req_status = "completed";
        } else if (running_count > 0 || sent_count > 0) {
            req_status = "processing";
        } else {
            req_status = "waiting";
        }
        req_json["waiting"] = waiting_count;
        req_json["processing"] = running_count;
        req_json["result_ready"] = 0;
        req_json["rst_sended"] = sent_count;
        req_json["status"] = req_status;
        req_json["sub_req_ids"] = sub_id_arr;

        auto &group = grouped[req.client_ip];
        if (group.is_null()) {
            group = json::object();
            group["client_ip"] = req.client_ip;
            group["reqs"] = json::array();
        }
        group["reqs"].push_back(req_json);
    }

    if (client_ip.empty()) {
        json clients = json::array();
        for (auto &entry : grouped) {
            clients.push_back(entry.second);
        }
        out["clients"] = clients;
    } else {
        auto it = grouped.find(client_ip);
        if (it != grouped.end()) {
            out = it->second;
        } else {
            out["client_ip"] = client_ip;
            out["reqs"] = json::array();
        }
    }
    return out;
}

std::optional<nlohmann::json> ReqManager::BuildReqDetail(const std::string &req_id) const {
    ReqRecord req_rec;
    if (!repo_.GetRequest(req_id, &req_rec)) {
        return std::nullopt;
    }
    const auto &req = req_rec.req;
    json req_json;
    req_json["req_id"] = req.req_id;
    req_json["client_ip"] = req.client_ip;
    req_json["tasktype"] = TaskTypeToString(req.task_type);
    req_json["total"] = req.total_num;
    int sent_count = 0;
    int running_count = 0;
    int waiting_count = 0;
    bool all_completed = true;
    bool any_failed = false;
    json sub_arr = json::array();

    std::vector<SubReqRecord> subs;
    if (repo_.ListSubRequestsByReq(req.req_id, &subs)) {
        all_completed = !subs.empty();
        for (const auto &sub_rec : subs) {
            const auto &sub = sub_rec.sub_req;
            const int task_total = sub.sub_req_count;
            int sub_sent = 0;
            int sub_running = 0;
            int sub_waiting = 0;
            if (sub_rec.status == "failed") {
                any_failed = true;
                all_completed = false;
            } else if (sub_rec.status == "completed") {
                sub_sent = task_total;
            } else if (sub_rec.status == "rst_sending" || sub_rec.status == "running") {
                sub_running = task_total;
                all_completed = false;
            } else {
                sub_waiting = task_total;
                all_completed = false;
            }
            sent_count += sub_sent;
            running_count += sub_running;
            waiting_count += sub_waiting;

            std::string sub_status;
            if (sub_rec.status == "failed") {
                sub_status = "failed";
            } else if (sub_rec.status == "completed") {
                sub_status = "completed";
            } else if (sub_rec.status == "rst_sending") {
                sub_status = "rst_sending";
            } else if (sub_rec.status == "running") {
                sub_status = "processing";
            } else {
                sub_status = "waiting";
            }
            json sub_json;
            sub_json["sub_req_id"] = sub.sub_req_id;
            sub_json["device_id"] = sub.dst_device_id == boost::uuids::nil_uuid()
                                        ? ""
                                        : boost::uuids::to_string(sub.dst_device_id);
            sub_json["device_ip"] = sub.dst_device_ip;
            sub_json["total_task_num"] = task_total;
            sub_json["waiting_task_num"] = sub_waiting;
            sub_json["processing_task_num"] = sub_running;
            sub_json["result_ready_task_num"] = 0;
            sub_json["rst_sended_task_num"] = sub_sent;
            sub_json["status"] = sub_status;
            sub_arr.push_back(sub_json);
        }
    } else {
        all_completed = false;
    }

    std::string req_status;
    if (any_failed) {
        req_status = "failed";
    } else if (all_completed && !sub_arr.empty()) {
        req_status = "completed";
    } else if (running_count > 0 || sent_count > 0) {
        req_status = "processing";
    } else {
        req_status = "waiting";
    }
    req_json["waiting"] = waiting_count;
    req_json["processing"] = running_count;
    req_json["result_ready"] = 0;
    req_json["rst_sended"] = sent_count;
    req_json["status"] = req_status;
    req_json["sub_reqs"] = sub_arr;
    return req_json;
}

std::optional<nlohmann::json> ReqManager::BuildSubReqDetail(const std::string &sub_req_id) const {
    SubReqRecord rec;
    if (!repo_.GetSubRequest(sub_req_id, &rec)) {
        return std::nullopt;
    }
    const auto &sub = rec.sub_req;
    json out;
    out["sub_req_id"] = sub.sub_req_id;
    out["req_id"] = sub.req_id;
    out["client_ip"] = sub.client_ip;
    out["device_id"] = sub.dst_device_id == boost::uuids::nil_uuid()
                           ? ""
                           : boost::uuids::to_string(sub.dst_device_id);
    out["device_ip"] = sub.dst_device_ip;
    out["attempt"] = sub.attempt;
    out["total_task_num"] = sub.sub_req_count;
    out["start_index"] = sub.start_index;
    out["failed"] = rec.status == "failed";
    if (rec.status == "failed" && !rec.fail_reason.empty()) {
        out["fail_reason"] = rec.fail_reason;
    }
    int sent = 0;
    int running = 0;
    int waiting = 0;
    if (rec.status == "failed") {
        waiting = 0;
    } else if (rec.status == "completed") {
        sent = sub.sub_req_count;
    } else if (rec.status == "rst_sending" || rec.status == "running") {
        running = sub.sub_req_count;
    } else {
        waiting = sub.sub_req_count;
    }
    out["waiting_task_num"] = waiting;
    out["processing_task_num"] = running;
    out["result_ready_task_num"] = 0;
    out["rst_sended_task_num"] = sent;

    std::string status;
    if (rec.status == "failed") {
        status = "failed";
    } else if (rec.status == "completed") {
        status = "completed";
    } else if (rec.status == "rst_sending") {
        status = "rst_sending";
    } else if (running > 0 || sent > 0) {
        status = "processing";
    } else {
        status = "queued";
    }
    out["status"] = status;
    return out;
}

nlohmann::json ReqManager::BuildNodesSnapshot() const {
    json out;
    json nodes = json::array();
    std::unordered_set<std::string> pending_ids;
    for (const auto &sub_req_id : GetPendingSubReqIds()) {
        pending_ids.insert(sub_req_id);
    }
    std::unordered_map<std::string, json> sub_reqs_by_device;
    std::vector<SubReqRecord> sub_records;
    if (repo_.ListSubRequests(&sub_records)) {
        for (const auto &rec : sub_records) {
            const auto &sub = rec.sub_req;
            const std::string dev_id_str = (sub.dst_device_id == boost::uuids::nil_uuid())
                                               ? ""
                                               : boost::uuids::to_string(sub.dst_device_id);
            if (dev_id_str.empty()) {
                continue;
            }
            const int task_total = sub.sub_req_count;
            int sub_sent = 0;
            int sub_running = 0;
            int sub_waiting = 0;
            if (rec.status == "failed") {
                sub_waiting = 0;
            } else if (rec.status == "completed") {
                sub_sent = task_total;
            } else if (rec.status == "rst_sending" || rec.status == "running") {
                sub_running = task_total;
            } else {
                sub_waiting = task_total;
            }
            std::string sub_status;
            if (rec.status == "failed") {
                sub_status = "failed";
            } else if (rec.status == "completed") {
                sub_status = "completed";
            } else if (rec.status == "rst_sending") {
                sub_status = "rst_sending";
            } else if (rec.status == "running") {
                sub_status = "processing";
            } else {
                sub_status = "waiting";
            }
            std::string lifecycle_state;
            if (rec.status == "failed") {
                lifecycle_state = "failed";
            } else if (rec.status == "completed") {
                lifecycle_state = "completed";
            } else if (rec.status == "rst_sending") {
                lifecycle_state = "rst_sending";
            } else if (pending_ids.find(sub.sub_req_id) != pending_ids.end()) {
                lifecycle_state = "pending_master";
            } else if (rec.status == "running") {
                lifecycle_state = "running";
            } else if (sub_sent > 0) {
                lifecycle_state = "rst_sended";
            } else {
                lifecycle_state = "assigned";
            }
            json sub_json;
            sub_json["sub_req_id"] = sub.sub_req_id;
            sub_json["req_id"] = sub.req_id;
            sub_json["client_ip"] = sub.client_ip;
            sub_json["total_task_num"] = task_total;
            sub_json["waiting_task_num"] = sub_waiting;
            sub_json["processing_task_num"] = sub_running;
            sub_json["result_ready_task_num"] = 0;
            sub_json["rst_sended_task_num"] = sub_sent;
            sub_json["status"] = sub_status;
            sub_json["lifecycle_state"] = lifecycle_state;
            auto &arr = sub_reqs_by_device[dev_id_str];
            if (arr.is_null()) {
                arr = json::array();
            }
            arr.push_back(sub_json);
        }
    }

    auto &node_manager = GetNodeManager();
    std::shared_lock<std::shared_mutex> lock(node_manager.Mutex());
    auto &device_static_info = node_manager.DeviceStaticInfo();
    auto &device_active_services = node_manager.ActiveServices();
    auto &device_status = node_manager.DeviceStatusMap();
    for (const auto &pair : device_static_info) {
        const DeviceID &dev_id = pair.first;
        const Device &dev = pair.second;
        json node;
        node["device_id"] = boost::uuids::to_string(dev_id);
        node["device_ip"] = dev.ip_address;
        node["device_type"] = to_string(nlohmann::json(dev.type));

        json services = json::array();
        auto svc_it = device_active_services.find(dev_id);
        if (svc_it != device_active_services.end()) {
            for (const auto &svc : svc_it->second) {
                services.push_back(to_string(nlohmann::json(svc)));
            }
        }
        node["services"] = services;

        json metrics;
        auto status_it = device_status.find(dev_id);
        if (status_it != device_status.end()) {
            const auto &status = status_it->second;
            metrics["cpu_used"] = status.cpu_used;
            metrics["mem_used"] = status.mem_used;
            metrics["xpu_used"] = status.xpu_used;
            metrics["net_latency_ms"] = status.net_latency;
            metrics["net_bandwidth_mbps"] = status.net_bandwidth;
            node["status"] = "online";
        } else {
            metrics["cpu_used"] = 0.0;
            metrics["mem_used"] = 0.0;
            metrics["xpu_used"] = 0.0;
            metrics["net_latency_ms"] = 0.0;
            metrics["net_bandwidth_mbps"] = 0.0;
            node["status"] = "offline";
        }
        node["metrics"] = metrics;

        const std::string dev_id_str = boost::uuids::to_string(dev_id);
        auto sub_it = sub_reqs_by_device.find(dev_id_str);
        if (sub_it != sub_reqs_by_device.end()) {
            node["sub_reqs"] = sub_it->second;
            node["sub_req_count"] = static_cast<int>(sub_it->second.size());
        } else {
            node["sub_reqs"] = json::array();
            node["sub_req_count"] = 0;
        }
        nodes.push_back(node);
    }
    out["nodes"] = nodes;
    return out;
}

std::string ReqManager::ResolveTaskTypeForSubReq(const std::string &sub_req_id) const {
    std::string tasktype;
    if (GetSubReqMeta(sub_req_id, &tasktype, nullptr, nullptr, nullptr)) {
        return tasktype;
    }
    SubReqRecord rec;
    if (repo_.GetSubRequest(sub_req_id, &rec)) {
        return TaskTypeToString(rec.sub_req.task_type);
    }
    return "";
}
void ReqManager::HandleUploadStreamChunk(StreamState &state,
                                         const std::string &payload,
                                         const std::string &peer) {
    json request = json::parse(payload, nullptr, false);
    if (request.is_discarded()) {
        return;
    }
    std::string filename_hint = request.value("filename", "");
    std::string content_b64 = request.value("content_b64", "");
    if (content_b64.empty()) {
        return;
    }
    if (state.client_ip.empty()) {
        state.client_ip = NormalizeClientIp(peer);
    }
    if (state.req_id.empty()) {
        std::string req_id = request.value("req_id", "");
        state.req_id = req_id.empty() ? MakeReqId(state.client_ip) : req_id;
    }
    if (state.tasktype.empty()) {
        state.tasktype = request.value("tasktype", kDefaultTaskType);
    }
    if (state.total_num == 0) {
        int total = request.value("total_num", 0);
        state.total_num = total > 0 ? total : 0;
    }

    std::string saved_path;
    std::string filename;
    if (!SaveImage(state.client_ip, filename_hint, content_b64, state.tasktype, state.req_id, saved_path, filename)) {
        return;
    }
    state.saved_count += 1;
    state.filenames.push_back(filename);
}

// when strem end, enqueue the request for scheduling and reply to client
std::string ReqManager::FinalizeUploadStream(StreamState &state) {
    if (state.saved_count > 0) {
        int total = state.total_num;
        if (total <= 0 || total != state.saved_count) {
            total = state.saved_count;
        }
        ScheduleItem item;
        item.client_ip = state.client_ip;
        item.tasktype = state.tasktype.empty() ? kDefaultTaskType : state.tasktype;
        item.req_id = state.req_id;
        item.req_dir = (std::filesystem::path(upload_root_) / SafeName(item.tasktype) / SafeName(item.client_ip) / SafeName(item.req_id)).string();
        item.total_num = total;
        item.filenames = std::move(state.filenames);
        SubmitSchedule(item);
    }
    json resp;
    resp["saved_count"] = state.saved_count;
    if (!state.req_id.empty()) {
        resp["req_id"] = state.req_id;
    }
    return resp.dump();
}

int ReqManager::NextSequence(const std::string &tasktype, const std::string &client_ip, const std::string &req_id) {
    const std::string key = SafeName(tasktype) + "/" + SafeName(client_ip) + "/" + SafeName(req_id);
    std::lock_guard<std::mutex> lock(seq_mutex_);
    int next = seq_map_[key] + 1;
    seq_map_[key] = next;
    return next;
}

bool ReqManager::SaveImage(const std::string &client_ip,
                           const std::string &filename_hint,
                           const std::string &content_b64,
                           const std::string &tasktype,
                           const std::string &req_id,
                           std::string &saved_path_out,
                           std::string &filename_out) {
    std::string decoded;
    if (!DecodeBase64(content_b64, decoded)) {
        return false;
    }
    std::filesystem::path upload_root(upload_root_);
    std::string task_dir = SafeName(tasktype);
    std::string req_dir = SafeName(req_id);
    std::string ip_dir = SafeName(client_ip);
    std::filesystem::path dir_path = upload_root / task_dir / ip_dir / req_dir;
    std::filesystem::create_directories(dir_path);

    std::filesystem::path hint(filename_hint);
    std::string ext = hint.has_extension() ? hint.extension().string() : "";
    int seq = NextSequence(tasktype, client_ip, req_id);
    std::string filename = ip_dir + "_" + req_dir + "_" + std::to_string(seq) + ext;
    std::filesystem::path save_path = dir_path / filename;

    std::ofstream out(save_path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
    out.close();

    saved_path_out = save_path.string();
    filename_out = filename;
    return true;
}

std::string ReqManager::SaveResultFile(const std::string &client_ip,
                                       const std::string &tasktype,
                                       const std::string &req_id,
                                       const std::string &sub_req_id,
                                       const std::string &file_name,
                                       const std::string &content) const {
    std::filesystem::path base(output_root_);
    std::string task_dir = SafeName(tasktype);
    std::string ip_dir = SafeName(client_ip);
    std::string req_dir = SafeName(req_id);
    std::string sub_dir = SafeName(sub_req_id);
    std::filesystem::path dir_path = base / task_dir / ip_dir / req_dir / sub_dir;
    std::filesystem::create_directories(dir_path);

    std::filesystem::path fname(file_name);
    std::string safe_name = fname.filename().string();
    std::filesystem::path save_path = dir_path / safe_name;

    std::ofstream out(save_path, std::ios::binary);
    if (!out) {
        return "";
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return save_path.string();
}

std::string ReqManager::MakeReqId(const std::string &client_ip) const {
    boost::uuids::uuid id = boost::uuids::random_generator()();
    return client_ip + "_" + boost::uuids::to_string(id);
}

void ReqManager::SubmitSchedule(const ScheduleItem &item) {
    if (item.client_ip.empty() || item.req_id.empty()) {
        spdlog::warn("ReqManager schedule skipped (missing ip/req_id)");
        return;
    }

    ClientRequest creq;
    creq.req_id = item.req_id;
    creq.client_ip = item.client_ip;
    creq.task_type = StrToTaskType(item.tasktype);
    creq.schedule_strategy = strategy_ == "roundrobin" ? ScheduleStrategy::ROUND_ROBIN : ScheduleStrategy::LOAD_BASED;
    creq.enqueue_time_ms = NowMs();
    creq.total_num = item.total_num;
    creq.req_dir = item.req_dir;
    creq.file_names = item.filenames;
    if (creq.file_names.empty() && !creq.req_dir.empty()) {
        std::filesystem::path base(creq.req_dir);
        if (std::filesystem::exists(base) && std::filesystem::is_directory(base)) {
            for (const auto &entry : std::filesystem::directory_iterator(base)) {
                if (entry.is_regular_file()) {
                    creq.file_names.push_back(entry.path().filename().string());
                }
            }
            std::sort(creq.file_names.begin(), creq.file_names.end());
        }
    }
    if (creq.file_names.empty()) {
        spdlog::warn("ReqManager schedule skipped (no files) req_id={}", creq.req_id);
        return;
    }
    if (creq.total_num <= 0) {
        creq.total_num = static_cast<int>(creq.file_names.size());
    }

    EnqueueRequest(creq);
    spdlog::info("ReqManager queued req_id={} files={} client_ip={}",
                 creq.req_id, creq.file_names.size(), creq.client_ip);
}

void ReqManager::PushReqPending(const ClientRequest &req) {
    {
        std::lock_guard<std::mutex> lock(req_mutex_);
        req_pending_.push_back(req);
    }
    req_cv_.notify_one();
}

ClientRequest ReqManager::PopReqPending() {
    std::unique_lock<std::mutex> lock(req_mutex_);
    req_cv_.wait(lock, [this]() { return stop_.load() || !req_pending_.empty(); });
    if (stop_.load()) {
        return ClientRequest{};
    }
    ClientRequest req = std::move(req_pending_.front());
    req_pending_.pop_front();
    return req;
}

void ReqManager::PushSubPending(const SubRequest &sub_req, bool high_priority) {
    {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        sub_req_store_[sub_req.sub_req_id] = sub_req;
        if (high_priority) {
            sub_pending_queue_.push_front(sub_req.sub_req_id);
        } else {
            sub_pending_queue_.push_back(sub_req.sub_req_id);
        }
    }
    sub_cv_.notify_one();
}

bool ReqManager::PopSubPending(SubRequest &out) {
    std::unique_lock<std::mutex> lock(sub_mutex_);
    sub_cv_.wait(lock, [this]() { return stop_.load() || !sub_pending_queue_.empty(); });
    if (stop_.load()) {
        return false;
    }
    while (!sub_pending_queue_.empty()) {
        std::string sub_req_id = sub_pending_queue_.front();
        sub_pending_queue_.pop_front();
        auto it = sub_req_store_.find(sub_req_id);
        if (it != sub_req_store_.end()) {
            out = it->second;
            return true;
        }
    }
    return false;
}

void ReqManager::AddRunningSubReq(const DeviceID &device_id, const SubRequest &sub_req) {
    std::lock_guard<std::mutex> lock(sub_mutex_);
    sub_req_store_[sub_req.sub_req_id] = sub_req;
    sub_running_[device_id].push_back(sub_req.sub_req_id);
}

int ReqManager::CountInFlightSubReqs(const DeviceID &device_id) const {
    std::lock_guard<std::mutex> lock(sub_mutex_);
    auto it = sub_running_.find(device_id);
    if (it == sub_running_.end()) {
        return 0;
    }
    return static_cast<int>(it->second.size());
}

SubRequest ReqManager::BuildRetrySubRequest(const SubRequest &old_sub_req, const std::string &reason) {
    int attempt = old_sub_req.attempt + 1;
    SubReqRecord rec;
    if (repo_.GetSubRequest(old_sub_req.sub_req_id, &rec)) {
        attempt = rec.sub_req.attempt + 1;
    }
    repo_.MarkSubRequestFailed(old_sub_req.sub_req_id, reason);

    SubRequest new_sub_req;
    new_sub_req.req_id = old_sub_req.req_id;
    new_sub_req.client_ip = old_sub_req.client_ip;
    new_sub_req.task_type = old_sub_req.task_type;
    new_sub_req.schedule_strategy = old_sub_req.schedule_strategy;
    new_sub_req.sub_req_count = old_sub_req.sub_req_count;
    new_sub_req.start_index = old_sub_req.start_index;
    new_sub_req.enqueue_time_ms = NowMs();
    new_sub_req.attempt = attempt;
    new_sub_req.planned_device_id = boost::uuids::nil_uuid();
    new_sub_req.planned_device_ip.clear();
    new_sub_req.dst_device_id = boost::uuids::nil_uuid();
    new_sub_req.dst_device_ip.clear();
    new_sub_req.sub_req_id = "retry_" + old_sub_req.sub_req_id + "_" + std::to_string(attempt);

    repo_.UpsertSubRequest(new_sub_req, "pending");
    return new_sub_req;
}
