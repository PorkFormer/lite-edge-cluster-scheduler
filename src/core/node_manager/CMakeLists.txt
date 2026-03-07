add_library(node_manager
        NodeManager.cpp
)

target_include_directories(node_manager
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(node_manager
        PUBLIC
        device_struct
        httplib::httplib
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        Boost::uuid
)
