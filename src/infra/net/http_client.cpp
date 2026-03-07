#include "infra/net/http_client.h"

#include <sstream>

std::string HttpClient::BuildUrl(const std::string &host, int port, const std::string &path) {
    std::ostringstream oss;
    oss << "http://" << host;
    if (port > 0) {
        oss << ":" << port;
    }
    if (!path.empty() && path[0] != '/') {
        oss << "/";
    }
    oss << path;
    return oss.str();
}
