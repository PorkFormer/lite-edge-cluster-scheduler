#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

class ReqManager;

class ReqQuery {
public:
    explicit ReqQuery(ReqManager &manager);

    nlohmann::json GetReqList(const std::string &client_ip) const;
    std::optional<nlohmann::json> GetReqDetail(const std::string &req_id) const;
    std::optional<nlohmann::json> GetSubReqDetail(const std::string &sub_req_id) const;
    nlohmann::json GetNodesSnapshot() const;

private:
    ReqManager &manager_;
};
