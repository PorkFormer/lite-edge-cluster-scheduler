add_executable(platform
        main.cpp
        platform/Platform.cpp
        http/HttpServer.cpp
        grpc/GrpcServer.cpp
)

target_include_directories(platform
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(platform
        PRIVATE
        req_manager
        services
        gRPC::grpc++
        gRPC::grpc
        nlohmann_json::nlohmann_json
        httplib::httplib
        thread_safe_map
        device_struct
        infra_net
        time_tools
        spdlog::spdlog
)



