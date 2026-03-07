add_library(req_manager
        ReqManager.cpp
        scheduler/Scheduler.cpp
        dispatcher/Dispatcher.cpp
        result_sender/ResultSender.cpp
        scheduler/engine/SchedulerEngine.cpp
        scheduler/strategy/LoadBasedStrategy.cpp
        scheduler/strategy/RoundRobinStrategy.cpp
        ../../infra/db/req_repository.cpp
        ../../infra/db/sqlite_db.cpp
)

target_include_directories(req_manager
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(req_manager
        PUBLIC
        node_manager
        httplib::httplib
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        Boost::uuid
        SQLite::SQLite3
)
