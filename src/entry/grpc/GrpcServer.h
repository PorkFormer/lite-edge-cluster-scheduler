#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "domain/config/config.h"
#include "services/req_ingress/ReqIngress.h"

class GrpcServer {
public:
    GrpcServer(const Args &args, ReqIngress &ingress);

    void Start();
    void Stop();

private:
    void Run();

    class CallData;

    ReqIngress &ingress_;
    std::string addr_;

    std::atomic<bool> started_{false};
    std::atomic<bool> stop_{false};

    std::thread grpc_thread_;
    grpc::AsyncGenericService service_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    std::unique_ptr<grpc::Server> server_;
};
