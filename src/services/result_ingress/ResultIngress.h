#pragma once

#include <string>

class ReqManager;

class ResultIngress {
public:
    explicit ResultIngress(ReqManager &manager);

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
                       std::string seq) const;

    void MarkSubReqResultsReady(const std::string &sub_req_id) const;
    std::string ResolveTaskTypeForSubReq(const std::string &sub_req_id) const;

private:
    ReqManager &manager_;
};
