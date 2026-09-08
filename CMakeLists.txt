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

add_library(gate_server_modules STATIC
    GateServer/GateServer/AsioIOServicePool.cpp
    GateServer/GateServer/CServer.cpp
    GateServer/GateServer/GateResponse.cpp
    GateServer/GateServer/HttpConnection.cpp
    GateServer/GateServer/LogicSystem.cpp
    GateServer/GateServer/GateRequest.cpp
    GateServer/GateServer/GateRequestProduction.cpp
    GateServer/GateServer/ConfigMgr.cpp
    GateServer/GateServer/StatusGrpcClient.cpp
    GateServer/GateServer/VerifyGrpcClient.cpp
    GateServer/GateServer/LogMgr.cpp
    GateServer/GateServer/MysqlDao.cpp
    GateServer/GateServer/MysqlMgr.cpp
    GateServer/GateServer/RedisMgr.cpp
)
chat_configure_server_target(gate_server_modules GateServer/GateServer)
add_library(chat::gate_server_modules ALIAS gate_server_modules)
add_executable(GateServer GateServer/GateServer/GateServer.cpp)
target_link_libraries(GateServer PRIVATE gate_server_modules)

add_library(status_server_modules STATIC
    StatusServer/StatusServer/AsioIOServicePool.cpp
    StatusServer/StatusServer/ConfigMgr.cpp
    StatusServer/StatusServer/DistLock.cpp
    StatusServer/StatusServer/LogMgr.cpp
    StatusServer/StatusServer/MysqlDao.cpp
    StatusServer/StatusServer/MysqlMgr.cpp
    StatusServer/StatusServer/RedisMgr.cpp
    StatusServer/StatusServer/Singleton.cpp
    StatusServer/StatusServer/StatusGrpcServer.cpp
    StatusServer/StatusServer/StatusServiceImpl.cpp
    StatusServer/StatusServer/StatusRouting.cpp
    StatusServer/StatusServer/StatusRoutingProduction.cpp
)
chat_configure_server_target(status_server_modules StatusServer/StatusServer)
add_library(chat::status_server_modules ALIAS status_server_modules)
add_executable(StatusServer StatusServer/StatusServer/StatusServer.cpp)
target_link_libraries(StatusServer PRIVATE status_server_modules)

add_library(chat_server_modules STATIC
    ChatServer/ChatServer/AsioIOServicePool.cpp
    ChatServer/ChatServer/ChatServiceImpl.cpp
    ChatServer/ChatServer/DistLock.cpp
    ChatServer/ChatServer/LogicSystem.cpp
    ChatServer/ChatServer/LogMgr.cpp
    ChatServer/ChatServer/MysqlDao.cpp
    ChatServer/ChatServer/MysqlMgr.cpp
    ChatServer/ChatServer/RedisMgr.cpp
    ChatServer/ChatServer/Singleton.cpp
    ChatServer/ChatServer/UserMgr.cpp
    ChatServer/ChatServer/ChatFrameCodec.cpp
    ChatServer/ChatServer/CServer.cpp
    ChatServer/ChatServer/CSession.cpp
    ChatServer/ChatServer/MsgNode.cpp
    ChatServer/ChatServer/LogicDispatcher.cpp
    ChatServer/ChatServer/ChatSessionState.cpp
    ChatServer/ChatServer/ChatSessionStateProduction.cpp
    ChatServer/ChatServer/ChatGrpcClient.cpp
    ChatServer/ChatServer/ConfigMgr.cpp
    ChatServer/ChatServer/Const.cpp
    ChatServer/ChatServer/PeerServerRouting.cpp
    ChatServer/ChatServer/StatusGrpcClient.cpp
)
chat_configure_server_target(chat_server_modules ChatServer/ChatServer)
add_library(chat::chat_server_modules ALIAS chat_server_modules)
add_executable(ChatServer ChatServer/ChatServer/ChatServer.cpp)
target_link_libraries(ChatServer PRIVATE chat_server_modules)

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
