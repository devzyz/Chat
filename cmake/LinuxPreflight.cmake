set(CHAT_LINUX_PREFLIGHT_SCHEMA_VERSION 1)
set(CHAT_LINUX_HOSTED_RUNNER "ubuntu-24.04")
set(CHAT_LINUX_COMPILER_ID "GNU")
set(CHAT_LINUX_COMPILER_VERSION "13.3.0")
set(CHAT_LINUX_CMAKE_VERSION "3.28.3")
set(CHAT_LINUX_QT_VERSION "6.5.3")
set(CHAT_LINUX_QT_ACQUISITION "jurplel/install-qt-action@48d3ad6db93f3627c8ee7a0454bc6f3744f7e730")
set(CHAT_LINUX_VCPKG_BASELINE "fc3be1ebea7eaeb3071fe716ac65713af1f3a146")
set(CHAT_LINUX_VCPKG_TRIPLET "x64-linux-chat-release")
set(CHAT_LINUX_NODE_VERSION "22.18.0")

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

    foreach(required_target IN LISTS CHAT_LINUX_EXPECTED_TARGETS)
        if(NOT TARGET ${required_target})
            message(FATAL_ERROR
                "LINUX_PREFLIGHT_BLOCKED: missing production target ${required_target}")
        endif()
    endforeach()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL CHAT_LINUX_COMPILER_ID OR
       NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL CHAT_LINUX_COMPILER_VERSION)
        message(FATAL_ERROR
            "LINUX_PREFLIGHT_BLOCKED: compiler identity mismatch; expected "
            "${CHAT_LINUX_COMPILER_ID} ${CHAT_LINUX_COMPILER_VERSION}, got "
            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    endif()

    if(NOT CMAKE_VERSION VERSION_EQUAL CHAT_LINUX_CMAKE_VERSION)
        message(FATAL_ERROR
            "LINUX_PREFLIGHT_BLOCKED: CMake identity mismatch; expected "
            "${CHAT_LINUX_CMAKE_VERSION}, got ${CMAKE_VERSION}")
    endif()

    if(NOT Qt6Core_VERSION VERSION_EQUAL CHAT_LINUX_QT_VERSION)
        message(FATAL_ERROR
            "LINUX_PREFLIGHT_BLOCKED: Qt identity mismatch; expected "
            "${CHAT_LINUX_QT_VERSION}, got ${Qt6Core_VERSION}")
    endif()

    file(REAL_PATH "${CMAKE_SOURCE_DIR}/.ci/vcpkg_installed" expected_installed_root)
    file(REAL_PATH "${VCPKG_INSTALLED_DIR}" actual_installed_root)
    if(NOT actual_installed_root STREQUAL expected_installed_root)
        message(FATAL_ERROR
            "LINUX_PREFLIGHT_BLOCKED: vcpkg install root is not run-owned")
    endif()
endfunction()
