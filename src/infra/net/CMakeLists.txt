add_library(infra_net STATIC
        DockerClient.cpp
        http_client.cpp
)

target_include_directories(infra_net
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(infra_net
        PUBLIC
        httplib::httplib
        spdlog::spdlog
        PRIVATE
        nlohmann_json::nlohmann_json
)
