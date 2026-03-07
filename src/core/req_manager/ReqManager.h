#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/uuid/uuid_hash.hpp>

#include "domain/config/config.h"
#include "infra/db/req_repository.h"
#include "core/req_manager/scheduler/engine/SchedulerEngine.h"
#include "domain/req/ResultItem.h"
#include "core/node_manager/NodeManager.h"
#include "core/req_manager/scheduler/Scheduler.h"
#include "core/req_manager/dispatcher/Dispatcher.h"
#include "core/req_manager/result_sender/ResultSender.h"

class ReqManager {
public:
    explicit ReqManager(const Args &args, NodeManager &node_manager);

    void Start();
    void Stop();

    void EnqueueRequest(const ClientRequest &req);
    std::string SaveResultFile(const std::string &client_ip,
                               const std::string &tasktype,
                               const std::string &req_id,
                               const std::string &sub_req_id,
                               const std::string &file_name,
                               const std::string &content) const;
    void EnqueueResult(std::string client_ip,
                       int client_port,
                       std::string service,
                       std::string file_name,
                       std::string file_path,
                       std::string content_type,
                       std::string req_id,
                       std::string sub_req_id,
                       std::string task_id,
                       std::string seq);

    void CompleteSubReq(const std::string &sub_req_id, const std::string &status);
    void MarkSubReqResultsReady(const std::string &sub_req_id);
    void RecoverTasks(const DeviceID &device_id);

    NodeManager &GetNodeManager();

    std::vector<std::string> GetPendingSubReqIds() const;
    nlohmann::json BuildReqList(const std::string &client_ip) const;
    std::optional<nlohmann::json> BuildReqDetail(const std::string &req_id) const;
    std::optional<nlohmann::json> BuildSubReqDetail(const std::string &sub_req_id) const;
    nlohmann::json BuildNodesSnapshot() const;
    std::string ResolveTaskTypeForSubReq(const std::string &sub_req_id) const;

    static void SetInstance(ReqManager *instance);
    static ReqManager &Get();

public:
    struct StreamState {
        std::string client_ip;
        std::string tasktype;
        std::string req_id;
        int total_num = 0;
        int saved_count = 0;
        std::vector<std::string> filenames;
    };

    void HandleUploadStreamChunk(StreamState &state,
                                 const std::string &payload,
                                 const std::string &peer);
    std::string FinalizeUploadStream(StreamState &state);

private:
    friend class SchedulerWorker;
    friend class DispatcherWorker;
    friend class ResultSenderWorker;

    struct ScheduleItem {
        std::string client_ip;
        std::string tasktype;
        std::string req_id;
        std::string req_dir;
        int total_num = 0;
        std::vector<std::string> filenames;
    };

    using ResultItem = ::ResultItem;

    struct SubReqResultState {
        std::string client_ip;
        int client_port = 0;
        std::string service;
        std::string req_id;
        std::string sub_req_id;
        std::string tasktype;
        int expected_count = 0;
        bool ready_signal = false;
        std::vector<std::string> file_names;
        std::vector<std::string> file_paths;
        std::vector<std::string> content_types;
    };

    void StartWorkers();

    int NextSequence(const std::string &tasktype, const std::string &client_ip, const std::string &req_id);
    bool SaveImage(const std::string &client_ip,
                   const std::string &filename_hint,
                   const std::string &content_b64,
                   const std::string &tasktype,
                   const std::string &req_id,
                   std::string &saved_path_out,
                   std::string &filename_out);
    std::string MakeReqId(const std::string &client_ip) const;
    void SubmitSchedule(const ScheduleItem &item);

    void PushReqPending(const ClientRequest &req);
    ClientRequest PopReqPending();

    void PushSubPending(const SubRequest &sub_req, bool high_priority);
    bool PopSubPending(SubRequest &out);

    void AddRunningSubReq(const DeviceID &device_id, const SubRequest &sub_req);
    int CountInFlightSubReqs(const DeviceID &device_id) const;

    SubRequest BuildRetrySubRequest(const SubRequest &old_sub_req, const std::string &reason);
    bool GetSubReqMeta(const std::string &sub_req_id,
                       std::string *out_tasktype,
                       std::string *out_req_id,
                       std::string *out_client_ip,
                       int *out_expected_count) const;
    bool TryBuildResultItemLocked(SubReqResultState &state, ResultItem &out);

    std::string upload_root_;
    std::string output_root_;
    std::string strategy_;
    std::string db_path_;

    std::atomic<bool> started_{false};
    std::atomic<bool> stop_{false};
    std::thread scheduler_thread_;
    std::thread dispatcher_thread_;
    std::thread result_sender_thread_;

    mutable std::mutex req_mutex_;
    std::condition_variable req_cv_;
    // 1.pending in master wait for scheduling, not necessarily in db yet
    std::deque<ClientRequest> req_pending_;

    mutable std::mutex sub_mutex_;
    std::condition_variable sub_cv_;
    // key: sub_req_id
    std::unordered_map<std::string, SubRequest> sub_req_store_;
    // 2.sub_reqs pending for dispatching
    std::deque<std::string> sub_pending_queue_;
    // 3.sub_reqs currently running on devices, used for retry and recovery
    // device_id â† list<sub_req_id>
    std::unordered_map<DeviceID, std::list<std::string>, boost::hash<DeviceID>> sub_running_;

    mutable std::mutex rst_mutex_;
    std::condition_variable rst_cv_;
    // 4.results waiting to be sent back to clients
    std::deque<ResultItem> rst_queue_;
    mutable std::mutex rst_state_mutex_;
    std::unordered_map<std::string, SubReqResultState> rst_states_;

    std::mutex seq_mutex_;
    std::unordered_map<std::string, int> seq_map_;

    ReqRepository repo_;
    NodeManager *node_manager_;

    static ReqManager *instance_;

    SchedulerWorker scheduler_worker_;
    DispatcherWorker dispatcher_worker_;
    ResultSenderWorker result_sender_worker_;
};
