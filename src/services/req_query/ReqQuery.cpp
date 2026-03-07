#include "services/req_query/ReqQuery.h"

#include "core/req_manager/ReqManager.h"

ReqQuery::ReqQuery(ReqManager &manager) : manager_(manager) {}

nlohmann::json ReqQuery::GetReqList(const std::string &client_ip) const {
    return manager_.BuildReqList(client_ip);
}

std::optional<nlohmann::json> ReqQuery::GetReqDetail(const std::string &req_id) const {
    return manager_.BuildReqDetail(req_id);
}

std::optional<nlohmann::json> ReqQuery::GetSubReqDetail(const std::string &sub_req_id) const {
    return manager_.BuildSubReqDetail(sub_req_id);
}

nlohmann::json ReqQuery::GetNodesSnapshot() const {
    return manager_.BuildNodesSnapshot();
}
