cmake_minimum_required(VERSION 3.28.3)

project(ChatServices VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Plan 3C-00-T1 deliberately freezes the inventory before the production
# targets exist.  LinuxPreflight.cmake keeps this a fail-closed RED contract.
set(CHAT_LINUX_PRODUCTION_TARGETS_READY OFF)

include(cmake/LinuxPreflight.cmake)
chat_register_linux_preflight_contract()

