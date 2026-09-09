cmake_minimum_required(VERSION 3.28.3)

project(ChatServices VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "LINUX_PREFLIGHT_BLOCKED: linux-x64-release must configure on hosted Ubuntu")
endif()

find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)
find_package(Threads REQUIRED)
# The root preflight checks this variable; discovery in chat/ has child scope.
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core)

if(TARGET unofficial::mysql-connector-cpp::connector-jdbc)
    set(CHAT_MYSQL_TARGET unofficial::mysql-connector-cpp::connector-jdbc)
else()
    message(FATAL_ERROR "LINUX_PREFLIGHT_BLOCKED: mysql connector JDBC target is missing")
endif()

set(CHAT_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(CHAT_PROTOCOL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/proto/cpp")
file(MAKE_DIRECTORY "${CHAT_PROTOCOL_OUTPUT_DIR}")
set(CHAT_PROTOCOL_SOURCES "")
set(CHAT_PROTOCOL_HEADERS "")
foreach(proto_relative IN ITEMS proto/varify.proto proto/status.proto proto/chat.proto)
    get_filename_component(proto_name "${proto_relative}" NAME_WE)
    set(proto_file "${CMAKE_CURRENT_SOURCE_DIR}/${proto_relative}")
    set(proto_source "${CHAT_PROTOCOL_OUTPUT_DIR}/${proto_name}.pb.cc")
    set(proto_header "${CHAT_PROTOCOL_OUTPUT_DIR}/${proto_name}.pb.h")
    set(grpc_source "${CHAT_PROTOCOL_OUTPUT_DIR}/${proto_name}.grpc.pb.cc")
    set(grpc_header "${CHAT_PROTOCOL_OUTPUT_DIR}/${proto_name}.grpc.pb.h")
    add_custom_command(
        OUTPUT "${proto_source}" "${proto_header}" "${grpc_source}" "${grpc_header}"
        COMMAND protobuf::protoc
        ARGS
            "--proto_path=${CHAT_PROTOCOL_DIR}"
            "--cpp_out=${CHAT_PROTOCOL_OUTPUT_DIR}"
            "--grpc_out=${CHAT_PROTOCOL_OUTPUT_DIR}"
            "--plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
            "${proto_file}"
        DEPENDS "${proto_file}" protobuf::protoc gRPC::grpc_cpp_plugin
        VERBATIM
    )
    list(APPEND CHAT_PROTOCOL_SOURCES "${proto_source}" "${grpc_source}")
    list(APPEND CHAT_PROTOCOL_HEADERS "${proto_header}" "${grpc_header}")
endforeach()

add_library(chat_protocol_cpp STATIC ${CHAT_PROTOCOL_SOURCES} ${CHAT_PROTOCOL_HEADERS})
target_include_directories(chat_protocol_cpp PUBLIC "${CHAT_PROTOCOL_OUTPUT_DIR}")
target_link_libraries(chat_protocol_cpp PUBLIC protobuf::libprotobuf gRPC::grpc++)

# Reuse the existing Qt production libraries without copying their sources.
add_subdirectory(chat)
add_library(client_modules INTERFACE)
target_link_libraries(client_modules INTERFACE
    chat_network_core
    chat_message_model
    chat_session_core
)
add_library(chat::client_modules ALIAS client_modules)

function(chat_configure_server_target target source_root)
    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/${source_root}"
        "${CHAT_PROTOCOL_OUTPUT_DIR}"
    )
    target_compile_definitions(${target} PUBLIC
        BOOST_ASIO_NO_DEPRECATED
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
    )
    target_link_libraries(${target} PUBLIC
        chat_protocol_cpp
        Boost::filesystem
        gRPC::grpc++
        hiredis::hiredis
        JsonCpp::JsonCpp
        ${CHAT_MYSQL_TARGET}
        spdlog::spdlog
        Threads::Threads
    )
endfunction()

include(cmake/ServerSourceOwnership.cmake)
chat_check_source_ownership("${CMAKE_CURRENT_SOURCE_DIR}")
chat_add_project_library(GateTransport GateServer/GateServer/GateTransport)
chat_add_project_library(GateRequest GateServer/GateServer/GateRequest)
chat_add_project_library(GateGrpcClients GateServer/GateServer/GateGrpcClients)
target_link_libraries(GateRequest PUBLIC GateGrpcClients)
chat_add_project_library(gate_server_modules GateServer/GateServer/GateServer GateServer/GateServer/GateServer.cpp)
target_link_libraries(gate_server_modules PUBLIC GateTransport GateRequest GateGrpcClients)
add_library(chat::gate_server_modules ALIAS gate_server_modules)
add_executable(GateServer GateServer/GateServer/GateServer.cpp)
target_link_libraries(GateServer PRIVATE "$<LINK_GROUP:RESCAN,gate_server_modules,GateTransport,GateRequest,GateGrpcClients>")

chat_add_project_library(StatusRouting StatusServer/StatusServer/StatusRouting)
chat_add_project_library(StatusTransport StatusServer/StatusServer/StatusTransport)
target_link_libraries(StatusTransport PUBLIC StatusRouting)
chat_add_project_library(status_server_modules StatusServer/StatusServer/StatusServer StatusServer/StatusServer/StatusServer.cpp)
target_link_libraries(status_server_modules PUBLIC StatusTransport StatusRouting)
add_library(chat::status_server_modules ALIAS status_server_modules)
add_executable(StatusServer StatusServer/StatusServer/StatusServer.cpp)
target_link_libraries(StatusServer PRIVATE "$<LINK_GROUP:RESCAN,status_server_modules,StatusTransport,StatusRouting>")

chat_add_project_library(ChatTransport ChatServer/ChatServer/ChatTransport)
chat_add_project_library(ChatSessionState ChatServer/ChatServer/ChatSessionState)
chat_add_project_library(LogicDispatcher ChatServer/ChatServer/LogicDispatcher)
chat_add_project_library(ChatGrpcClients ChatServer/ChatServer/ChatGrpcClients)
target_link_libraries(ChatTransport PUBLIC ChatSessionState LogicDispatcher)
chat_add_project_library(chat_server_modules ChatServer/ChatServer/ChatServer ChatServer/ChatServer/ChatServer.cpp)
target_link_libraries(chat_server_modules PUBLIC ChatTransport ChatSessionState LogicDispatcher ChatGrpcClients)
add_library(chat::chat_server_modules ALIAS chat_server_modules)
add_executable(ChatServer ChatServer/ChatServer/ChatServer.cpp)
target_link_libraries(ChatServer PRIVATE "$<LINK_GROUP:RESCAN,chat_server_modules,ChatTransport,ChatSessionState,LogicDispatcher,ChatGrpcClients>")

enable_testing()
add_library(process_harness STATIC
    tests/support/RunContext.cpp
    tests/support/ProcessHarness.cpp
    tests/support/PosixProcessAdapter.cpp)
target_include_directories(process_harness PUBLIC tests/support)
target_link_libraries(process_harness PUBLIC Boost::filesystem Threads::Threads)
add_library(integration_host STATIC tests/support/IntegrationHostFactory.cpp)
target_include_directories(integration_host PUBLIC tests/support)
target_link_libraries(integration_host PUBLIC
    GateTransport GateRequest StatusTransport StatusRouting ChatTransport ChatSessionState LogicDispatcher)
find_package(GTest CONFIG REQUIRED)
add_executable(integration_host_contract_tests tests/server/integration-host/integration_host_contract_tests.cpp)
target_link_libraries(integration_host_contract_tests PRIVATE integration_host GTest::gtest_main)
add_test(NAME T10-BLD-02-integration-host COMMAND integration_host_contract_tests)
set_tests_properties(T10-BLD-02-integration-host PROPERTIES LABELS "phase3c-build-ownership" TIMEOUT 30)
add_subdirectory(tests/server/process-harness)
add_executable(process_harness_posix_tests tests/server/process-harness/process_harness_posix_tests.cpp)
target_link_libraries(process_harness_posix_tests PRIVATE process_harness)
add_executable(service_run tests/services/ServiceRun.cpp)
target_link_libraries(service_run PRIVATE process_harness)
add_executable(redis_adapter_integration tests/server/data/redis_adapter_integration.cpp)
target_link_libraries(redis_adapter_integration PRIVATE hiredis::hiredis Threads::Threads)
add_test(NAME phase3c-redis-lifecycle COMMAND redis_adapter_integration lifecycle)
set_tests_properties(phase3c-redis-lifecycle PROPERTIES LABELS "phase3c-redis-local" TIMEOUT 10)
add_test(NAME T10-BLD-03-process-harness COMMAND process_harness_posix_tests)
set_tests_properties(T10-BLD-03-process-harness PROPERTIES LABELS "phase3c-build-ownership" TIMEOUT 15)

set(CHAT_LINUX_EXPECTED_TARGETS
    chat_protocol_cpp
    gate_server_modules
    status_server_modules
    chat_server_modules
    GateServer
    StatusServer
    ChatServer
    chat_network_core
    chat_message_model
    chat_session_core
)

set(CHAT_LINUX_PRODUCTION_TARGETS_READY ON)

include(cmake/LinuxPreflight.cmake)
chat_register_linux_preflight_contract()
