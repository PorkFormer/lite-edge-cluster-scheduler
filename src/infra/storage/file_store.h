#pragma once

#include <string>

class FileStore {
public:
    static std::string JoinPath(const std::string &base, const std::string &child);
};
