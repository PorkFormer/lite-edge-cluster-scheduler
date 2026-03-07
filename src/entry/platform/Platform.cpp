#include "Platform.h"

#include <chrono>

#include <spdlog/spdlog.h>

Platform::Platform(std::string http_ip, int http_port, const Args &args)
    : http_ip_(std::move(http_ip)),
      http_port_(http_port),
      args_(args),
      node_manager_(),
      req_manager_(args_, node_manager_),
      req_ingress_(req_manager_),
      result_ingress_(req_manager_),
      req_query_(req_manager_),
      node_service_(),
      grpc_server_(args_, req_ingress_),
      http_server_(http_ip_, http_port_, args_, node_service_, req_query_, result_ingress_) {}

void Platform::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }

    SchedulerEngine::SetNodeManager(&node_manager_);
    SchedulerEngine::Init(args_.config_path + "/static_info.json");
    node_manager_.StartDeviceInfoCollection();

    ReqManager::SetInstance(&req_manager_);

    http_thread_ = std::thread([this]() {
        if (!http_server_.Start()) {
            spdlog::error("HttpServer failed to start");
        }
    });

    grpc_server_.Start();
    req_manager_.Start();
}

void Platform::Stop() {
    grpc_server_.Stop();
    req_manager_.Stop();
    http_server_.Stop();
    if (http_thread_.joinable()) {
        http_thread_.join();
    }
}
