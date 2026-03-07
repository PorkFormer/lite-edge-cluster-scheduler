#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain/enums/ScheduleStrategy.h"
#include "domain/enums/TaskType.h"

struct ClientRequest {
    std::string req_id;
    std::string client_ip;
    TaskType task_type{TaskType::Unknown};
    ScheduleStrategy schedule_strategy{ScheduleStrategy::LOAD_BASED};
    std::string req_dir;
    int total_num{0};
    int64_t enqueue_time_ms{0};
    std::vector<std::string> file_names;
    std::vector<std::string> sub_req_ids;
};
