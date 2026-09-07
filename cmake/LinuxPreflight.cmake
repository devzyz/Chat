set(CHAT_LINUX_PREFLIGHT_SCHEMA_VERSION 1)
set(CHAT_LINUX_HOSTED_RUNNER "ubuntu-24.04")
set(CHAT_LINUX_COMPILER_ID "GNU")
set(CHAT_LINUX_COMPILER_VERSION "13.3.0")
set(CHAT_LINUX_CMAKE_VERSION "3.28.3")
set(CHAT_LINUX_QT_VERSION "6.5.3")
set(CHAT_LINUX_QT_ACQUISITION "jurplel/install-qt-action@48d3ad6db93f3627c8ee7a0454bc6f3744f7e730")
set(CHAT_LINUX_VCPKG_BASELINE "fc3be1ebea7eaeb3071fe716ac65713af1f3a146")
set(CHAT_LINUX_VCPKG_TRIPLET "x64-linux-chat-release")
set(CHAT_LINUX_NODE_VERSION "22")

function(chat_register_linux_preflight_contract)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "Linux preflight target registration is inactive on ${CMAKE_SYSTEM_NAME}")
        return()
    endif()

    if(NOT CHAT_LINUX_PRODUCTION_TARGETS_READY)
        message(FATAL_ERROR
            "LINUX_PREFLIGHT_BLOCKED: production target registry is incomplete; "
            "GateServer, StatusServer, ChatServer, Qt modules, canonical proto, and Varify startup proof are required")
    endif()
endfunction()

