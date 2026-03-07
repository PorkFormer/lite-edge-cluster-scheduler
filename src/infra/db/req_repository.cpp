#include "req_repository.h"

#include <chrono>

#include <boost/uuid/string_generator.hpp>
#include <spdlog/spdlog.h>

namespace {
constexpr const char *kCreateRequests = R"sql(
CREATE TABLE IF NOT EXISTS requests (
    req_id TEXT PRIMARY KEY,
    client_ip TEXT,
    task_type TEXT,
    strategy TEXT,
    req_dir TEXT,
    total_num INTEGER,
    enqueue_time_ms INTEGER,
    status TEXT,
    created_at INTEGER,
    updated_at INTEGER
);
)sql";

constexpr const char *kCreateRequestFiles = R"sql(
CREATE TABLE IF NOT EXISTS request_files (
    req_id TEXT NOT NULL,
    filename TEXT NOT NULL,
    PRIMARY KEY (req_id, filename)
);
)sql";

constexpr const char *kCreateSubRequests = R"sql(
CREATE TABLE IF NOT EXISTS sub_requests (
    sub_req_id TEXT PRIMARY KEY,
    req_id TEXT,
    client_ip TEXT,
    task_type TEXT,
    strategy TEXT,
    sub_req_count INTEGER,
    start_index INTEGER,
    enqueue_time_ms INTEGER,
    expected_end_time_ms INTEGER,
    attempt INTEGER,
    planned_device_id TEXT,
    planned_device_ip TEXT,
    dst_device_id TEXT,
    dst_device_ip TEXT,
    status TEXT,
    fail_reason TEXT,
    created_at INTEGER,
    updated_at INTEGER
);
)sql";

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string TaskTypeToString(TaskType t) {
    return t == TaskType::Unknown ? "Unknown" : to_string(nlohmann::json(t));
}

std::string StrategyToString(ScheduleStrategy s) {
    return s == ScheduleStrategy::ROUND_ROBIN ? "roundrobin" : "load";
}

ScheduleStrategy StrategyFromString(const std::string &s) {
    return s == "roundrobin" ? ScheduleStrategy::ROUND_ROBIN : ScheduleStrategy::LOAD_BASED;
}

TaskType TaskTypeFromString(const std::string &s) {
    return StrToTaskType(s);
}

bool ReadText(sqlite3_stmt *stmt, int idx, std::string *out) {
    if (!out) {
        return false;
    }
    const unsigned char *text = sqlite3_column_text(stmt, idx);
    if (!text) {
        out->clear();
        return true;
    }
    out->assign(reinterpret_cast<const char *>(text));
    return true;
}

int ReadInt(sqlite3_stmt *stmt, int idx) {
    return sqlite3_column_int(stmt, idx);
}

int64_t ReadInt64(sqlite3_stmt *stmt, int idx) {
    return sqlite3_column_int64(stmt, idx);
}
}  // namespace

ReqRepository::ReqRepository(std::string db_path) : db_(std::move(db_path)) {}

/// @brief  Initialize the repository, create tables if not exist
/// @return true if success, false otherwise
bool ReqRepository::Init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    if (!PrepareSchema()) {
        return false;
    }
    db_.Exec("PRAGMA journal_mode=WAL;");
    db_.Exec("PRAGMA synchronous=NORMAL;");
    return true;
}

bool ReqRepository::PrepareSchema() {
    return db_.Exec(kCreateRequests) &&
           db_.Exec(kCreateRequestFiles) &&
           db_.Exec(kCreateSubRequests);
}

bool ReqRepository::BindText(sqlite3_stmt *stmt, int idx, const std::string &value) {
    return sqlite3_bind_text(stmt, idx, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool ReqRepository::BindInt(sqlite3_stmt *stmt, int idx, int value) {
    return sqlite3_bind_int(stmt, idx, value) == SQLITE_OK;
}

bool ReqRepository::BindInt64(sqlite3_stmt *stmt, int idx, int64_t value) {
    return sqlite3_bind_int64(stmt, idx, value) == SQLITE_OK;
}

bool ReqRepository::UpsertRequest(const ClientRequest &req) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
INSERT INTO requests(req_id, client_ip, task_type, strategy, req_dir, total_num, enqueue_time_ms, status, created_at, updated_at)
VALUES(?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(req_id) DO UPDATE SET
    client_ip=excluded.client_ip,
    task_type=excluded.task_type,
    strategy=excluded.strategy,
    req_dir=excluded.req_dir,
    total_num=excluded.total_num,
    enqueue_time_ms=excluded.enqueue_time_ms,
    status=excluded.status,
    updated_at=excluded.updated_at;
)sql";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("sqlite prepare UpsertRequest failed");
        return false;
    }
    int64_t now = NowMs();
    BindText(stmt, 1, req.req_id);
    BindText(stmt, 2, req.client_ip);
    BindText(stmt, 3, TaskTypeToString(req.task_type));
    BindText(stmt, 4, StrategyToString(req.schedule_strategy));
    BindText(stmt, 5, req.req_dir);
    BindInt(stmt, 6, req.total_num);
    BindInt64(stmt, 7, req.enqueue_time_ms);
    BindText(stmt, 8, "pending");
    BindInt64(stmt, 9, now);
    BindInt64(stmt, 10, now);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::ReplaceRequestFiles(const std::string &req_id, const std::vector<std::string> &files) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *del_sql = "DELETE FROM request_files WHERE req_id=?;";
    sqlite3_stmt *del_stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), del_sql, -1, &del_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(del_stmt, 1, req_id);
    sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);

    const char *ins_sql = "INSERT INTO request_files(req_id, filename) VALUES(?,?);";
    sqlite3_stmt *ins_stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), ins_sql, -1, &ins_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    for (const auto &file : files) {
        sqlite3_reset(ins_stmt);
        sqlite3_clear_bindings(ins_stmt);
        BindText(ins_stmt, 1, req_id);
        BindText(ins_stmt, 2, file);
        if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
            sqlite3_finalize(ins_stmt);
            return false;
        }
    }
    sqlite3_finalize(ins_stmt);
    return true;
}

bool ReqRepository::UpdateRequestStatus(const std::string &req_id, const std::string &status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = "UPDATE requests SET status=?, updated_at=? WHERE req_id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, status);
    BindInt64(stmt, 2, NowMs());
    BindText(stmt, 3, req_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::UpsertSubRequest(const SubRequest &sub_req, const std::string &status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
INSERT INTO sub_requests(sub_req_id, req_id, client_ip, task_type, strategy, sub_req_count, start_index, enqueue_time_ms,
                         expected_end_time_ms, attempt, planned_device_id, planned_device_ip, dst_device_id, dst_device_ip,
                         status, fail_reason, created_at, updated_at)
VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(sub_req_id) DO UPDATE SET
    req_id=excluded.req_id,
    client_ip=excluded.client_ip,
    task_type=excluded.task_type,
    strategy=excluded.strategy,
    sub_req_count=excluded.sub_req_count,
    start_index=excluded.start_index,
    enqueue_time_ms=excluded.enqueue_time_ms,
    expected_end_time_ms=excluded.expected_end_time_ms,
    attempt=excluded.attempt,
    planned_device_id=excluded.planned_device_id,
    planned_device_ip=excluded.planned_device_ip,
    dst_device_id=excluded.dst_device_id,
    dst_device_ip=excluded.dst_device_ip,
    status=excluded.status,
    fail_reason=excluded.fail_reason,
    updated_at=excluded.updated_at;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    int64_t now = NowMs();
    BindText(stmt, 1, sub_req.sub_req_id);
    BindText(stmt, 2, sub_req.req_id);
    BindText(stmt, 3, sub_req.client_ip);
    BindText(stmt, 4, TaskTypeToString(sub_req.task_type));
    BindText(stmt, 5, StrategyToString(sub_req.schedule_strategy));
    BindInt(stmt, 6, sub_req.sub_req_count);
    BindInt(stmt, 7, sub_req.start_index);
    BindInt64(stmt, 8, sub_req.enqueue_time_ms);
    BindInt64(stmt, 9, sub_req.expected_end_time_ms);
    BindInt(stmt, 10, sub_req.attempt);
    BindText(stmt, 11, sub_req.planned_device_id == boost::uuids::nil_uuid()
                      ? ""
                      : boost::uuids::to_string(sub_req.planned_device_id));
    BindText(stmt, 12, sub_req.planned_device_ip);
    BindText(stmt, 13, sub_req.dst_device_id == boost::uuids::nil_uuid()
                      ? ""
                      : boost::uuids::to_string(sub_req.dst_device_id));
    BindText(stmt, 14, sub_req.dst_device_ip);
    BindText(stmt, 15, status);
    BindText(stmt, 16, "");
    BindInt64(stmt, 17, now);
    BindInt64(stmt, 18, now);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::UpdateSubRequestDispatched(const std::string &sub_req_id, const Device &device) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
UPDATE sub_requests
SET dst_device_id=?, dst_device_ip=?, status=?, updated_at=?
WHERE sub_req_id=?;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, boost::uuids::to_string(device.global_id));
    BindText(stmt, 2, device.ip_address);
    BindText(stmt, 3, "running");
    BindInt64(stmt, 4, NowMs());
    BindText(stmt, 5, sub_req_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::MarkSubRequestRstSending(const std::string &sub_req_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = "UPDATE sub_requests SET status=?, updated_at=? WHERE sub_req_id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, "rst_sending");
    BindInt64(stmt, 2, NowMs());
    BindText(stmt, 3, sub_req_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::MarkSubRequestFailed(const std::string &sub_req_id, const std::string &reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
UPDATE sub_requests
SET status=?, fail_reason=?, updated_at=?
WHERE sub_req_id=?;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, "failed");
    BindText(stmt, 2, reason);
    BindInt64(stmt, 3, NowMs());
    BindText(stmt, 4, sub_req_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::MarkSubRequestCompleted(const std::string &sub_req_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = "UPDATE sub_requests SET status=?, updated_at=? WHERE sub_req_id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, "completed");
    BindInt64(stmt, 2, NowMs());
    BindText(stmt, 3, sub_req_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::GetRequest(const std::string &req_id, ReqRecord *out) {
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
SELECT req_id, client_ip, task_type, strategy, req_dir, total_num, enqueue_time_ms, status
FROM requests
WHERE req_id=?;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, req_id);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ClientRequest req;
        ReadText(stmt, 0, &req.req_id);
        ReadText(stmt, 1, &req.client_ip);
        std::string task_type;
        ReadText(stmt, 2, &task_type);
        std::string strategy;
        ReadText(stmt, 3, &strategy);
        ReadText(stmt, 4, &req.req_dir);
        req.total_num = ReadInt(stmt, 5);
        req.enqueue_time_ms = ReadInt64(stmt, 6);
        req.task_type = TaskTypeFromString(task_type);
        req.schedule_strategy = StrategyFromString(strategy);
        out->req = std::move(req);
        ReadText(stmt, 7, &out->status);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::ListRequests(const std::string &client_ip, std::vector<ReqRecord> *out) {
    if (!out) {
        return false;
    }
    out->clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql_all = R"sql(
SELECT req_id, client_ip, task_type, strategy, req_dir, total_num, enqueue_time_ms, status
FROM requests
ORDER BY created_at DESC;
)sql";
    const char *sql_filtered = R"sql(
SELECT req_id, client_ip, task_type, strategy, req_dir, total_num, enqueue_time_ms, status
FROM requests
WHERE client_ip=?
ORDER BY created_at DESC;
)sql";
    sqlite3_stmt *stmt = nullptr;
    const char *sql = client_ip.empty() ? sql_all : sql_filtered;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    if (!client_ip.empty()) {
        BindText(stmt, 1, client_ip);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReqRecord rec;
        ReadText(stmt, 0, &rec.req.req_id);
        ReadText(stmt, 1, &rec.req.client_ip);
        std::string task_type;
        ReadText(stmt, 2, &task_type);
        std::string strategy;
        ReadText(stmt, 3, &strategy);
        ReadText(stmt, 4, &rec.req.req_dir);
        rec.req.total_num = ReadInt(stmt, 5);
        rec.req.enqueue_time_ms = ReadInt64(stmt, 6);
        rec.req.task_type = TaskTypeFromString(task_type);
        rec.req.schedule_strategy = StrategyFromString(strategy);
        ReadText(stmt, 7, &rec.status);
        out->push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);
    return true;
}

bool ReqRepository::GetSubRequest(const std::string &sub_req_id, SubReqRecord *out) {
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
SELECT sub_req_id, req_id, client_ip, task_type, strategy, sub_req_count, start_index,
       enqueue_time_ms, expected_end_time_ms, attempt, planned_device_id, planned_device_ip,
       dst_device_id, dst_device_ip, status, fail_reason
FROM sub_requests
WHERE sub_req_id=?;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, sub_req_id);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        SubRequest sub;
        ReadText(stmt, 0, &sub.sub_req_id);
        ReadText(stmt, 1, &sub.req_id);
        ReadText(stmt, 2, &sub.client_ip);
        std::string task_type;
        ReadText(stmt, 3, &task_type);
        std::string strategy;
        ReadText(stmt, 4, &strategy);
        sub.sub_req_count = ReadInt(stmt, 5);
        sub.start_index = ReadInt(stmt, 6);
        sub.enqueue_time_ms = ReadInt64(stmt, 7);
        sub.expected_end_time_ms = ReadInt64(stmt, 8);
        sub.attempt = ReadInt(stmt, 9);
        std::string planned_id;
        std::string dst_id;
        ReadText(stmt, 10, &planned_id);
        ReadText(stmt, 11, &sub.planned_device_ip);
        ReadText(stmt, 12, &dst_id);
        ReadText(stmt, 13, &sub.dst_device_ip);
        sub.task_type = TaskTypeFromString(task_type);
        sub.schedule_strategy = StrategyFromString(strategy);
        if (!planned_id.empty()) {
            sub.planned_device_id = boost::uuids::string_generator()(planned_id);
        }
        if (!dst_id.empty()) {
            sub.dst_device_id = boost::uuids::string_generator()(dst_id);
        }
        ReadText(stmt, 14, &out->status);
        ReadText(stmt, 15, &out->fail_reason);
        out->sub_req = std::move(sub);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool ReqRepository::ListSubRequestsByReq(const std::string &req_id, std::vector<SubReqRecord> *out) {
    if (!out) {
        return false;
    }
    out->clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
SELECT sub_req_id, req_id, client_ip, task_type, strategy, sub_req_count, start_index,
       enqueue_time_ms, expected_end_time_ms, attempt, planned_device_id, planned_device_ip,
       dst_device_id, dst_device_ip, status, fail_reason
FROM sub_requests
WHERE req_id=?
ORDER BY created_at ASC;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, req_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SubReqRecord rec;
        ReadText(stmt, 0, &rec.sub_req.sub_req_id);
        ReadText(stmt, 1, &rec.sub_req.req_id);
        ReadText(stmt, 2, &rec.sub_req.client_ip);
        std::string task_type;
        ReadText(stmt, 3, &task_type);
        std::string strategy;
        ReadText(stmt, 4, &strategy);
        rec.sub_req.sub_req_count = ReadInt(stmt, 5);
        rec.sub_req.start_index = ReadInt(stmt, 6);
        rec.sub_req.enqueue_time_ms = ReadInt64(stmt, 7);
        rec.sub_req.expected_end_time_ms = ReadInt64(stmt, 8);
        rec.sub_req.attempt = ReadInt(stmt, 9);
        std::string planned_id;
        std::string dst_id;
        ReadText(stmt, 10, &planned_id);
        ReadText(stmt, 11, &rec.sub_req.planned_device_ip);
        ReadText(stmt, 12, &dst_id);
        ReadText(stmt, 13, &rec.sub_req.dst_device_ip);
        rec.sub_req.task_type = TaskTypeFromString(task_type);
        rec.sub_req.schedule_strategy = StrategyFromString(strategy);
        if (!planned_id.empty()) {
            rec.sub_req.planned_device_id = boost::uuids::string_generator()(planned_id);
        }
        if (!dst_id.empty()) {
            rec.sub_req.dst_device_id = boost::uuids::string_generator()(dst_id);
        }
        ReadText(stmt, 14, &rec.status);
        ReadText(stmt, 15, &rec.fail_reason);
        out->push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);
    return true;
}

bool ReqRepository::ListSubRequests(std::vector<SubReqRecord> *out) {
    if (!out) {
        return false;
    }
    out->clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
SELECT sub_req_id, req_id, client_ip, task_type, strategy, sub_req_count, start_index,
       enqueue_time_ms, expected_end_time_ms, attempt, planned_device_id, planned_device_ip,
       dst_device_id, dst_device_ip, status, fail_reason
FROM sub_requests
ORDER BY created_at ASC;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SubReqRecord rec;
        ReadText(stmt, 0, &rec.sub_req.sub_req_id);
        ReadText(stmt, 1, &rec.sub_req.req_id);
        ReadText(stmt, 2, &rec.sub_req.client_ip);
        std::string task_type;
        ReadText(stmt, 3, &task_type);
        std::string strategy;
        ReadText(stmt, 4, &strategy);
        rec.sub_req.sub_req_count = ReadInt(stmt, 5);
        rec.sub_req.start_index = ReadInt(stmt, 6);
        rec.sub_req.enqueue_time_ms = ReadInt64(stmt, 7);
        rec.sub_req.expected_end_time_ms = ReadInt64(stmt, 8);
        rec.sub_req.attempt = ReadInt(stmt, 9);
        std::string planned_id;
        std::string dst_id;
        ReadText(stmt, 10, &planned_id);
        ReadText(stmt, 11, &rec.sub_req.planned_device_ip);
        ReadText(stmt, 12, &dst_id);
        ReadText(stmt, 13, &rec.sub_req.dst_device_ip);
        rec.sub_req.task_type = TaskTypeFromString(task_type);
        rec.sub_req.schedule_strategy = StrategyFromString(strategy);
        if (!planned_id.empty()) {
            rec.sub_req.planned_device_id = boost::uuids::string_generator()(planned_id);
        }
        if (!dst_id.empty()) {
            rec.sub_req.dst_device_id = boost::uuids::string_generator()(dst_id);
        }
        ReadText(stmt, 14, &rec.status);
        ReadText(stmt, 15, &rec.fail_reason);
        out->push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);
    return true;
}

bool ReqRepository::GetRequestFiles(const std::string &req_id, std::vector<std::string> *out) {
    if (!out) {
        return false;
    }
    out->clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
SELECT filename
FROM request_files
WHERE req_id=?
ORDER BY rowid ASC;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, req_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string name;
        ReadText(stmt, 0, &name);
        out->push_back(std::move(name));
    }
    sqlite3_finalize(stmt);
    return true;
}

bool ReqRepository::GetRequestFileSlice(const std::string &req_id,
                                        int start_index,
                                        int count,
                                        std::string *req_dir,
                                        std::vector<std::string> *out) {
    if (!req_dir || !out || count <= 0 || start_index < 0) {
        return false;
    }
    ReqRecord req_rec;
    if (!GetRequest(req_id, &req_rec)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_.Open()) {
        return false;
    }
    const char *sql = R"sql(
SELECT filename
FROM request_files
WHERE req_id=?
ORDER BY rowid ASC
LIMIT ? OFFSET ?;
)sql";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    BindText(stmt, 1, req_id);
    BindInt(stmt, 2, count);
    BindInt(stmt, 3, start_index);
    out->clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string name;
        ReadText(stmt, 0, &name);
        out->push_back(std::move(name));
    }
    sqlite3_finalize(stmt);
    *req_dir = req_rec.req.req_dir;
    return !out->empty();
}
