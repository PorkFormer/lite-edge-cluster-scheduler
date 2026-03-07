add_executable(gateway
        main.cpp
        platform/Platform.cpp
        http/HttpServer.cpp
        grpc/GrpcServer.cpp
)

target_include_directories(gateway
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(gateway
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



