add_library(device_struct STATIC
        device.cpp
        ../enums/TaskType.cpp
)

target_include_directories(device_struct
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(device_struct
        time_tools
        nlohmann_json::nlohmann_json
        Boost::uuid
        Boost::assert Boost::config Boost::throw_exception Boost::type_traits Boost::static_assert
        spdlog::spdlog
)

target_include_directories(device_struct PRIVATE
        ${boost-uuid_SOURCE_DIR}/boost
)
