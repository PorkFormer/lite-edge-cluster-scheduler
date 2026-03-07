#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <optional>

#include "domain/req/ClientRequest.h"
#include "domain/req/SubRequest.h"
#include "domain/enums/ScheduleStrategy.h"
#include "infra/db/sqlite_db.h"

struct ReqRecord {
    ClientRequest req;
    std::string status;
};

struct SubReqRecord {
    SubRequest sub_req;
    std::string status;
    std::string fail_reason;
};

class ReqRepository {
public:
    explicit ReqRepository(std::string db_path);

    bool Init();
    bool UpsertRequest(const ClientRequest &req);
    bool ReplaceRequestFiles(const std::string &req_id, const std::vector<std::string> &files);
    bool UpdateRequestStatus(const std::string &req_id, const std::string &status);

    bool UpsertSubRequest(const SubRequest &sub_req, const std::string &status);
    bool UpdateSubRequestDispatched(const std::string &sub_req_id, const Device &device);
    bool MarkSubRequestRstSending(const std::string &sub_req_id);
    bool MarkSubRequestFailed(const std::string &sub_req_id, const std::string &reason);
    bool MarkSubRequestCompleted(const std::string &sub_req_id);

    bool GetRequest(const std::string &req_id, ReqRecord *out);
    bool ListRequests(const std::string &client_ip, std::vector<ReqRecord> *out);
    bool GetSubRequest(const std::string &sub_req_id, SubReqRecord *out);
    bool ListSubRequestsByReq(const std::string &req_id, std::vector<SubReqRecord> *out);
    bool ListSubRequests(std::vector<SubReqRecord> *out);
    bool GetRequestFiles(const std::string &req_id, std::vector<std::string> *out);
    bool GetRequestFileSlice(const std::string &req_id,
                             int start_index,
                             int count,
                             std::string *req_dir,
                             std::vector<std::string> *out);

private:
    bool PrepareSchema();
    bool BindText(sqlite3_stmt *stmt, int idx, const std::string &value);
    bool BindInt(sqlite3_stmt *stmt, int idx, int value);
    bool BindInt64(sqlite3_stmt *stmt, int idx, int64_t value);

    std::mutex mutex_;
    SqliteDb db_;
};
