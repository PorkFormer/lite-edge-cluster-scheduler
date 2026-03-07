#include "services/result_ingress/ResultIngress.h"

#include "core/req_manager/ReqManager.h"

ResultIngress::ResultIngress(ReqManager &manager) : manager_(manager) {}

std::string ResultIngress::SaveResultFile(const std::string &client_ip,
                                          const std::string &tasktype,
                                          const std::string &req_id,
                                          const std::string &sub_req_id,
                                          const std::string &file_name,
                                          const std::string &content) const {
    return manager_.SaveResultFile(client_ip, tasktype, req_id, sub_req_id, file_name, content);
}

void ResultIngress::EnqueueResult(std::string client_ip,
                                  int client_port,
                                  std::string service,
                                  std::string file_name,
                                  std::string file_path,
                                  std::string content_type,
                                  std::string req_id,
                                  std::string sub_req_id,
                                  std::string task_id,
                                  std::string seq) const {
    manager_.EnqueueResult(std::move(client_ip),
                           client_port,
                           std::move(service),
                           std::move(file_name),
                           std::move(file_path),
                           std::move(content_type),
                           std::move(req_id),
                           std::move(sub_req_id),
                           std::move(task_id),
                           std::move(seq));
}

void ResultIngress::MarkSubReqResultsReady(const std::string &sub_req_id) const {
    manager_.MarkSubReqResultsReady(sub_req_id);
}

std::string ResultIngress::ResolveTaskTypeForSubReq(const std::string &sub_req_id) const {
    return manager_.ResolveTaskTypeForSubReq(sub_req_id);
}
