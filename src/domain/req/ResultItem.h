#pragma once

#include <string>
#include <vector>

struct ResultItem {
    std::string client_ip;
    int client_port = 0;
    std::string service;
    std::string req_id;
    std::string sub_req_id;
    std::string tasktype;
    std::vector<std::string> file_names;
    std::vector<std::string> file_paths;
    std::vector<std::string> content_types;
};
