#include "core/req_manager/result_sender/ResultSender.h"

#include <fstream>
#include <sstream>

#include <httplib.h>
#include <spdlog/spdlog.h>

#include "core/req_manager/ReqManager.h"

ResultSenderWorker::ResultSenderWorker(ReqManager &manager) : manager_(manager) {}

void ResultSenderWorker::Run() {
    while (!manager_.stop_.load()) {
        ResultItem item;
        {
            std::unique_lock<std::mutex> lock(manager_.rst_mutex_);
            manager_.rst_cv_.wait(lock, [this]() { return manager_.stop_.load() || !manager_.rst_queue_.empty(); });
            if (manager_.stop_.load()) {
                break;
            }
            item = std::move(manager_.rst_queue_.front());
            manager_.rst_queue_.pop_front();
        }

        bool all_sent = true;
        for (size_t i = 0; i < item.file_paths.size(); ++i) {
            const std::string &file_path = item.file_paths[i];
            const std::string &file_name = item.file_names[i];
            const std::string &content_type = item.content_types[i];
            std::ifstream ifs(file_path, std::ios::binary);
            if (!ifs) {
                spdlog::warn("ResultSenderLoop: failed to open result file {}", file_path);
                all_sent = false;
                break;
            }
            std::ostringstream buffer;
            buffer << ifs.rdbuf();
            std::string content = buffer.str();

            httplib::Client cli(item.client_ip, item.client_port);
            httplib::MultipartFormDataItems form_items;
            form_items.push_back({"file", content, file_name, content_type.empty() ? "application/octet-stream" : content_type});
            if (!item.service.empty()) {
                form_items.push_back({"service", item.service, "", "text/plain"});
            }
            if (!item.req_id.empty()) {
                form_items.push_back({"req_id", item.req_id, "", "text/plain"});
            }
            if (!item.sub_req_id.empty()) {
                form_items.push_back({"sub_req_id", item.sub_req_id, "", "text/plain"});
            }

            auto res = cli.Post("/recv_rst", form_items);
            if (!res || res->status != 200) {
                spdlog::warn("Forward result failed: client={} status={}", item.client_ip, res ? res->status : -1);
                all_sent = false;
                break;
            }
        }

        if (all_sent && !item.sub_req_id.empty()) {
            manager_.CompleteSubReq(item.sub_req_id, "success");
        } else if (!all_sent) {
            spdlog::warn("ResultSenderLoop: partial send for sub_req {}", item.sub_req_id);
        }
    }
}
