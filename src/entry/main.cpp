#include "entry/platform/Platform.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_shutdown{false};

void OnSignal(int) {
    g_shutdown.store(true);
}
}  // namespace

static Args parse_arguments(int argc, char *argv[]) {
    Args args;
    args.config_path = "./myapp";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            args.config_path = argv[++i];
            continue;
        }
        if (arg == "--keep-upload") {
            args.keep_upload = true;
            continue;
        }
        if ((arg == "--grpc-port" || arg == "-gp") && i + 1 < argc) {
            try {
                args.grpc_port = std::stoi(argv[++i]);
            } catch (...) {
                args.grpc_port = 9999;
            }
            continue;
        }
        if ((arg == "--upload-root" || arg == "-u") && i + 1 < argc) {
            args.grpc_upload_root = argv[++i];
            continue;
        }
        if ((arg == "--strategy" || arg == "-s") && i + 1 < argc) {
            args.grpc_strategy = argv[++i];
            continue;
        }
        if ((arg == "--db" || arg == "-d") && i + 1 < argc) {
            args.db_path = argv[++i];
            continue;
        }
    }
    return args;
}

int main(int argc, char *argv[]) {
    Args args = parse_arguments(argc, argv);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("parse params config_path: {}, keep_upload: {}",
                 args.config_path, args.keep_upload);
    spdlog::info("grpc params: port={}, upload_root={}, strategy={}",
                 args.grpc_port, args.grpc_upload_root, args.grpc_strategy);
    spdlog::info("db params: path={}", args.db_path);

    const std::string addr = "0.0.0.0";
    const int port = 6666;
    Platform platform(addr, port, args);

    // listen SIGINT and SIGTERM for graceful shutdown
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    platform.Start();

    // main thread waits for shutdown signal
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    platform.Stop();

    return 0;
}
