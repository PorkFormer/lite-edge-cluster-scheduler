add_library(services
        req_ingress/ReqIngress.cpp
        result_ingress/ResultIngress.cpp
        req_query/ReqQuery.cpp
        node_service/NodeService.cpp
)

target_include_directories(services
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(services
        PUBLIC
        req_manager
        nlohmann_json::nlohmann_json
)
