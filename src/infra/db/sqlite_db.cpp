#include "sqlite_db.h"

#include <spdlog/spdlog.h>

SqliteDb::SqliteDb(std::string path) : path_(std::move(path)) {}

SqliteDb::~SqliteDb() {
    Close();
}

bool SqliteDb::Open() {
    if (db_) {
        return true;
    }
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) {
        spdlog::error("sqlite open failed: {}", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    return true;
}

void SqliteDb::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDb::Exec(const std::string &sql) {
    char *err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        if (err_msg) {
            spdlog::error("sqlite exec failed: {}", err_msg);
            sqlite3_free(err_msg);
        } else {
            spdlog::error("sqlite exec failed: unknown error");
        }
        return false;
    }
    return true;
}
