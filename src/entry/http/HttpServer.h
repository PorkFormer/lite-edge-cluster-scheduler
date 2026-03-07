#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <httplib.h>
#include "domain/config/config.h"
#include "services/node_service/NodeService.h"
#include "services/req_query/ReqQuery.h"
#include "services/result_ingress/ResultIngress.h"
#include "spdlog/spdlog.h"

// 用户请求ai任务处理
// node 
const std::string REGISTER_NODE_ROUTE = "/register_node";
const std::string DISCONNECT_NODE_ROUTE = "/unregister_node";

const std::string NODES_ROUTE = "/nodes";// get node info

// req/sub_req info
const std::string REQ_LIST_ROUTE = "/reqs"; // all req
const std::string REQ_DETAIL_ROUTE = "/req"; // 指定req_id
const std::string SUB_REQ_ROUTE = "/sub_req";// 指定sub_req_id

// communicate with slave device
const std::string RECV_RST_ROUTE = "/recv_rst"; // slave -- (rst) --> master
const std::string SUB_REQ_DONE_ROUTE = "/sub_req_done"; // slave -- (sub_req done flag) --> master

// 废弃接口，保留用于兼容旧版本客户端python task_manager module
const std::string SCHEDULE_ROUTE = "/schedule";

class HttpServer {
public:
    HttpServer(std::string ip, int port, const Args &args,
               NodeService &node_service,
               ReqQuery &req_query,
               ResultIngress &result_ingress);
    bool Start();
    void Stop();

private:
    static void HandleRegisterNode(const httplib::Request &req, httplib::Response &res);
    static void HandleDisconnect(const httplib::Request &req, httplib::Response &res);
    static void HandleReqList(const httplib::Request &req, httplib::Response &res);
    static void HandleReqDetail(const httplib::Request &req, httplib::Response &res);
    static void HandleSubReq(const httplib::Request &req, httplib::Response &res);
    static void HandleNodes(const httplib::Request &req, httplib::Response &res);
    static void HandleRecvResult(const httplib::Request &req, httplib::Response &res);
    static void HandleSubReqDone(const httplib::Request &req, httplib::Response &res);
    static void HandleSchedule(const httplib::Request &req, httplib::Response &res);

    void StartHealthCheckThread();
    void HealthCheckLoop();

    std::string ip;
    int port;
    std::unique_ptr<httplib::Server> server_;
    static Args args;
    static NodeService *node_service_;
    static ReqQuery *req_query_;
    static ResultIngress *result_ingress_;
    static std::thread health_check_thread_;
    static std::atomic<bool> health_check_stop_;
    static constexpr double HEALTH_CHECK_LATENCY_THRESHOLD = 10.0;  // 10秒延迟阈值
    static constexpr uint32_t HEALTH_CHECK_INTERVAL = 5000;         // 5秒检查间隔
    static constexpr uint32_t HEALTH_CHECK_COOLDOWN_SEC = 30;
};
