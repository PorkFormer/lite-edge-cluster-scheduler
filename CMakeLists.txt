cmake_minimum_required(VERSION 3.17)
project(docker_scheduler)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")

FetchContent_Declare(
        googletest
        SOURCE_DIR "${DEPS_ROOT}/googletest-src"
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        httplib
        SOURCE_DIR "${DEPS_ROOT}/httplib-src"
)

FetchContent_Declare(
        json
        SOURCE_DIR "${DEPS_ROOT}/json-src"
)

fetchcontent_declare(
        boost-uuid
        SOURCE_DIR "${DEPS_ROOT}/boost-uuid-src"
)

FetchContent_Declare(
        boost-assert
        SOURCE_DIR "${DEPS_ROOT}/boost-assert-src"
)

FetchContent_Declare(
        boost-config
        SOURCE_DIR "${DEPS_ROOT}/boost-config-src"
)

FetchContent_Declare(
        boost-throw_exception
        SOURCE_DIR "${DEPS_ROOT}/boost-throw-src"
)

FetchContent_Declare(
        boost-type_traits
        SOURCE_DIR "${DEPS_ROOT}/boost-traits-src"
)
FetchContent_Declare(
        boost-static_assert
        SOURCE_DIR "${DEPS_ROOT}/boost-static-src"
)

FetchContent_MakeAvailable(boost-uuid boost-assert boost-config boost-throw_exception boost-type_traits boost-static_assert)

FetchContent_Declare(
        spdlog
        SOURCE_DIR "${DEPS_ROOT}/spdlog-src"
)

FetchContent_Declare(
        grpc
        SOURCE_DIR "${DEPS_ROOT}/grpc-src"
)

set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(utf8_range_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest httplib json spdlog grpc)

set(SQLITE_ROOT "${DEPS_ROOT}/sqlite-src")
add_library(sqlite3 STATIC "${SQLITE_ROOT}/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${SQLITE_ROOT}")
add_library(SQLite::SQLite3 ALIAS sqlite3)

if (WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
endif ()


get_filename_component(ABSOLUTE_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config_files" ABSOLUTE)
add_subdirectory(src/utils/time)
add_subdirectory(src/utils/thread_safe_map)
add_subdirectory(src/domain/node)
add_subdirectory(src/infra/net)
add_subdirectory(src/core/node_manager)
add_subdirectory(src/core/req_manager)
add_subdirectory(src/services)
add_subdirectory(src/entry/agent)
add_subdirectory(src/entry)
target_compile_definitions(platform PUBLIC ABSOLUTE_CONFIG_PATH="${ABSOLUTE_CONFIG_PATH}")
enable_testing()
include(GoogleTest)
# add_subdirectory(tests/scheduler)
# add_subdirectory(tests/predict)
# 构建gtest end
