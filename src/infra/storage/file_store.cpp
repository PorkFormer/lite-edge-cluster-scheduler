#include "infra/storage/file_store.h"

#include <filesystem>

std::string FileStore::JoinPath(const std::string &base, const std::string &child) {
    return (std::filesystem::path(base) / child).string();
}
