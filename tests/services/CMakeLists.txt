cmake_minimum_required(VERSION 3.24)
project(ChatDisposableServices NONE)
enable_testing()
find_program(CHAT_NODE_EXECUTABLE node REQUIRED)
if(NOT EXISTS "$ENV{CHAT_SERVICE_LAUNCHER}")
    message(FATAL_ERROR "same-source service launcher artifact is required")
endif()
add_test(NAME phase3c-disposable-services
    COMMAND "$ENV{CHAT_SERVICE_LAUNCHER}" "${CHAT_NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/dependencyCoordinator.js" "$ENV{CHAT_SERVICE_EVIDENCE_ROOT}")
set_tests_properties(phase3c-disposable-services PROPERTIES LABELS "phase3c-services" TIMEOUT 690)
