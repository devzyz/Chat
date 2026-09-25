cmake_minimum_required(VERSION 3.28.3)
get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED CHAT_TEST_BINARY_DIR)
    set(CHAT_TEST_BINARY_DIR "${repo_root}/build/server-only-contract")
endif()
file(MAKE_DIRECTORY "${CHAT_TEST_BINARY_DIR}")
# Configure the real graph with dependency stand-ins; this is not a compile test.
# Forbid Qt/GTest discovery even when they happen to be installed on the host.
file(WRITE "${CHAT_TEST_BINARY_DIR}/dependencies.cmake" [=[
function(find_package name)
    if(name MATCHES "^(Qt|QT|GTest)")
        message(FATAL_ERROR "Server-only configuration discovered forbidden package: ${name}")
    endif()
    foreach(target IN ITEMS Boost::filesystem gRPC::grpc++ protobuf::libprotobuf
            hiredis::hiredis JsonCpp::JsonCpp unofficial::mysql-connector-cpp::connector-jdbc
            spdlog::spdlog Threads::Threads)
        if(NOT TARGET ${target})
            add_library(${target} INTERFACE IMPORTED GLOBAL)
        endif()
    endforeach()
    foreach(target IN ITEMS protobuf::protoc gRPC::grpc_cpp_plugin)
        if(NOT TARGET ${target})
            add_executable(${target} IMPORTED GLOBAL)
            set_target_properties(${target} PROPERTIES IMPORTED_LOCATION "${CMAKE_COMMAND}")
        endif()
    endforeach()
endfunction()
]=])
# Forced compiler metadata avoids SDK discovery and never invokes a compiler.
# Only generation is tested here; hosted CI separately compiles with real packages.
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${repo_root}" -B "${CHAT_TEST_BINARY_DIR}/configured"
    -G "Ninja"
    "-DCMAKE_MAKE_PROGRAM=${CHAT_TEST_NINJA}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_COMMAND}"
    -DCMAKE_CXX_COMPILER_FORCED=TRUE -DCMAKE_CXX_COMPILER_ID=GNU
    -DCMAKE_CXX_COMPILER_VERSION=13.3.0 -DCMAKE_CXX_ABI_COMPILED=TRUE
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PROJECT_ChatServices_INCLUDE_BEFORE=${CHAT_TEST_BINARY_DIR}/dependencies.cmake"
    -DCHAT_BUILD_CLIENT=OFF -DBUILD_TESTING=OFF -DCHAT_ENABLE_HOSTED_PREFLIGHT=OFF
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 45)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Headless configuration failed (${result}):\n${output}\n${error}")
endif()
file(READ "${CHAT_TEST_BINARY_DIR}/configured/build.ninja" graph)
foreach(server IN ITEMS GateServer StatusServer ChatServer)
    if(NOT graph MATCHES "build ${server}:")
        message(FATAL_ERROR "Missing server build target: ${server}")
    endif()
endforeach()
if(graph MATCHES "chat/packages/spdlog|Qt[56]|tests/server|chat_session_core")
    message(FATAL_ERROR "Server-only graph contains client or test dependencies")
endif()
message(STATUS "Server-only configuration excludes Qt, client sources, vendored logging and tests")
