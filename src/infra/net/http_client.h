#pragma once

#include <string>

class HttpClient {
public:
    static std::string BuildUrl(const std::string &host, int port, const std::string &path);
};
