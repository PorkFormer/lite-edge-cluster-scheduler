#pragma once

#include <cstdint>
#include <string>

#include <boost/uuid/uuid.hpp>

#include "domain/enums/ScheduleStrategy.h"
#include "domain/enums/TaskType.h"

struct SubRequest {
    std::string sub_req_id;
    std::string req_id;
    std::string client_ip;
    TaskType task_type{TaskType::Unknown};
    ScheduleStrategy schedule_strategy{ScheduleStrategy::LOAD_BASED};
    int sub_req_count{0};
    int start_index{0};
    int64_t enqueue_time_ms{0};
    int64_t expected_end_time_ms{0};
    int attempt{0};
    boost::uuids::uuid planned_device_id{};
    std::string planned_device_ip;
    boost::uuids::uuid dst_device_id{};
    std::string dst_device_ip;
};
