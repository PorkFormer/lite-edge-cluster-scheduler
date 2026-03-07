#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "domain/config/config.h"
#include "core/req_manager/ReqManager.h"
#include "core/req_manager/scheduler/engine/SchedulerEngine.h"
#include "entry/grpc/GrpcServer.h"
#include "entry/http/HttpServer.h"
#include "services/node_service/NodeService.h"
#include "services/req_ingress/ReqIngress.h"
#include "services/req_query/ReqQuery.h"
#include "services/result_ingress/ResultIngress.h"
#include "core/node_manager/NodeManager.h"

class Platform {
public:
    Platform(std::string http_ip, int http_port, const Args &args);

    void Start();
    void Stop();

private:
    std::string http_ip_;
    int http_port_;
    Args args_;

    ReqManager req_manager_;
    NodeManager node_manager_;
    ReqIngress req_ingress_;
    ResultIngress result_ingress_;
    ReqQuery req_query_;
    NodeService node_service_;
    GrpcServer grpc_server_;
    HttpServer http_server_;

    std::thread http_thread_;
    std::atomic<bool> started_{false};
};
