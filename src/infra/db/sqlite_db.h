#pragma once

#include <sqlite3.h>

#include <string>

class SqliteDb {
public:
    explicit SqliteDb(std::string path);
    ~SqliteDb();

    bool Open();
    void Close();
    bool Exec(const std::string &sql);

    sqlite3 *Handle() { return db_; }

private:
    std::string path_;
    sqlite3 *db_{nullptr};
};
