#pragma once

#include <string>

struct Args {
    // Directory containing config files (e.g. static_info.json).
    std::string config_path;

    // Keep uploaded files after successful completion (default: delete).
    bool keep_upload = false;

    // gRPC request receiver settings.
    int grpc_port = 9999;
    std::string grpc_upload_root = "workspace/master/Input";
    std::string grpc_strategy = "load";

    // SQLite db path for req/sub_req persistence.
    std::string db_path = "workspace/master/data/req_manager.db";
};
