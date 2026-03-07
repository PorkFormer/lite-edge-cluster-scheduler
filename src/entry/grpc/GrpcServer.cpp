#include "GrpcServer.h"

#include <sstream>
#include <vector>

#include "core/req_manager/ReqManager.h"

namespace {
std::string ByteBufferToString(const grpc::ByteBuffer &buffer) {
    std::vector<grpc::Slice> slices;
    buffer.Dump(&slices);
    std::string out;
    for (const auto &slice : slices) {
        out.append(reinterpret_cast<const char *>(slice.begin()), slice.size());
    }
    return out;
}

grpc::ByteBuffer StringToByteBuffer(const std::string &data) {
    grpc::Slice slice(data.data(), data.size());
    return grpc::ByteBuffer(&slice, 1);
}
}  // namespace

class GrpcServer::CallData {
public:
    CallData(grpc::AsyncGenericService *service,
             grpc::ServerCompletionQueue *cq,
             ReqIngress *ingress)
        : service_(service),
          cq_(cq),
          stream_(&ctx_),
          ingress_(ingress) {
        Proceed(true);
    }

    void Proceed(bool ok) {
        if (state_ == State::CREATE) {
            state_ = State::REQUEST;
            service_->RequestCall(&ctx_, &stream_, cq_, cq_, this);
            return;
        }

        if (state_ == State::REQUEST) {
            if (!ok) {
                delete this;
                return;
            }
            new CallData(service_, cq_, ingress_);
            state_ = State::READ_STREAM;
            stream_.Read(&request_, this);
            return;
        }

        if (state_ == State::READ_STREAM) {
            if (!ok) {
                std::string response = ingress_->Finalize(&stream_state_);
                response_ = StringToByteBuffer(response);
                state_ = State::WRITE;
                stream_.WriteAndFinish(response_, grpc::WriteOptions(), grpc::Status::OK, this);
                return;
            }
            std::string payload = ByteBufferToString(request_);
            ingress_->OnChunk(&stream_state_, payload, ctx_.peer());
            stream_.Read(&request_, this);
            return;
        }

        if (state_ == State::WRITE) {
            delete this;
            return;
        }
    }

private:
    enum class State {
        CREATE,
        REQUEST,
        READ_STREAM,
        WRITE
    };

    grpc::AsyncGenericService *service_;
    grpc::ServerCompletionQueue *cq_;
    grpc::GenericServerContext ctx_;
    grpc::GenericServerAsyncReaderWriter stream_;
    grpc::ByteBuffer request_;
    grpc::ByteBuffer response_;
    State state_{State::CREATE};
    ReqIngress *ingress_{nullptr};
    ReqManager::StreamState stream_state_;
};

GrpcServer::GrpcServer(const Args &args, ReqIngress &ingress)
    : ingress_(ingress),
      addr_("0.0.0.0:" + std::to_string(args.grpc_port > 0 ? args.grpc_port : 9999)) {}

void GrpcServer::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    stop_.store(false);
    grpc_thread_ = std::thread([this]() { Run(); });
}

void GrpcServer::Stop() {
    stop_.store(true);
    if (server_) {
        server_->Shutdown();
    }
    if (cq_) {
        cq_->Shutdown();
    }
    if (grpc_thread_.joinable()) {
        grpc_thread_.join();
    }
}

void GrpcServer::Run() {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr_, grpc::InsecureServerCredentials());
    builder.RegisterAsyncGenericService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    new CallData(&service_, cq_.get(), &ingress_);
    void *tag = nullptr;
    bool ok = false;
    while (cq_->Next(&tag, &ok)) {
        static_cast<CallData *>(tag)->Proceed(ok);
    }
}
