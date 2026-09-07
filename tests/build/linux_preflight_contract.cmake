cmake_minimum_required(VERSION 3.28.3)

foreach(required IN ITEMS CHAT_JUNIT_PATH CHAT_EVIDENCE_PATH CHAT_EXPECT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT CHAT_EXPECT MATCHES "^(RED|GREEN)$")
    message(FATAL_ERROR "CHAT_EXPECT must be RED or GREEN")
endif()

if(DEFINED CHAT_REPO_ROOT AND NOT "${CHAT_REPO_ROOT}" STREQUAL "")
    get_filename_component(repo_root "${CHAT_REPO_ROOT}" ABSOLUTE)
else()
    get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
file(READ "${repo_root}/CMakeLists.txt" root_cmake)
file(READ "${repo_root}/CMakePresets.json" presets)
file(READ "${repo_root}/triplets/x64-linux-chat-release.cmake" triplet)
file(READ "${repo_root}/cmake/LinuxPreflight.cmake" preflight)
file(READ "${repo_root}/vcpkg.json" manifest)
file(READ "${repo_root}/scripts/linux-ci.sh" runner)
file(READ "${repo_root}/.github/workflows/linux-ci.yml" workflow)

set(failures "")
set(junit_cases "")
set(failure_count 0)

function(has_text source needle output)
    string(FIND "${${source}}" "${needle}" found_at)
    if(found_at EQUAL -1)
        set(${output} FALSE PARENT_SCOPE)
    else()
        set(${output} TRUE PARENT_SCOPE)
    endif()
endfunction()

macro(record_case id passed diagnostic)
    if(${passed})
        string(APPEND junit_cases
            "<testcase classname=\"phase3c.preflight\" name=\"${id}\"/>")
    else()
        math(EXPR failure_count "${failure_count} + 1")
        list(APPEND failures "${diagnostic}")
        string(APPEND junit_cases
            "<testcase classname=\"phase3c.preflight\" name=\"${id}\">"
            "<failure message=\"LINUX_PREFLIGHT_BLOCKED\">${diagnostic}</failure>"
            "</testcase>")
    endif()
endmacro()

set(targets_present TRUE)
foreach(target IN ITEMS
        chat_protocol_cpp gate_server_modules status_server_modules chat_server_modules
        GateServer StatusServer ChatServer chat_network_core chat_message_model chat_session_core)
    has_text(root_cmake "${target}" target_present)
    if(NOT target_present)
        set(targets_present FALSE)
    endif()
endforeach()
if(NOT root_cmake MATCHES "set\\(CHAT_LINUX_PRODUCTION_TARGETS_READY[ \t\r\n]+ON\\)")
    set(targets_present FALSE)
endif()
record_case("T10-LNX-01-target-inventory" targets_present
    "missing production target/link/start ownership proof")

set(hosted_pin_ok FALSE)
if(workflow MATCHES "runs-on:[ \t]*ubuntu-24\\.04" AND
   NOT workflow MATCHES "ubuntu-latest|self-hosted")
    set(hosted_pin_ok TRUE)
endif()
record_case("T10-LNX-02-hosted-runner-pin" hosted_pin_ok
    "hosted Ubuntu runner label is absent, floating, or self-hosted")

set(compiler_ok FALSE)
if(preflight MATCHES "CHAT_LINUX_COMPILER_ID[ \t]+\"GNU\"" AND
   preflight MATCHES "CHAT_LINUX_COMPILER_VERSION[ \t]+\"13\\.3\\.0\"")
    set(compiler_ok TRUE)
endif()
record_case("T10-LNX-03-compiler-identity" compiler_ok
    "compiler identity is not pinned to GNU 13.3.0")

set(cmake_ok FALSE)
if(preflight MATCHES "CHAT_LINUX_CMAKE_VERSION[ \t]+\"3\\.28\\.3\"" AND
   presets MATCHES "linux-x64-release" AND
   workflow MATCHES "lukka/get-cmake@fffaaafeea488556c2c12dad60690008bc1caacb" AND
   workflow MATCHES "cmakeVersion:[ \t]+\"3\\.28\\.3\"")
    string(FIND "${workflow}"
        "lukka/get-cmake@fffaaafeea488556c2c12dad60690008bc1caacb"
        cmake_acquire_at)
    string(FIND "${workflow}" "Run fail-closed Linux preflight" preflight_run_at)
    if(cmake_acquire_at GREATER_EQUAL 0 AND preflight_run_at GREATER cmake_acquire_at)
        set(cmake_ok TRUE)
    endif()
endif()
record_case("T10-LNX-04-cmake-preset" cmake_ok
    "explicit full-SHA acquisition of locked CMake 3.28.3 is missing or ordered after preflight")

set(qt_ok FALSE)
if(preflight MATCHES "CHAT_LINUX_QT_VERSION[ \t]+\"6\\.5\\.3\"" AND
   workflow MATCHES "jurplel/install-qt-action@48d3ad6db93f3627c8ee7a0454bc6f3744f7e730")
    set(qt_ok TRUE)
endif()
record_case("T10-LNX-05-qt-acquisition" qt_ok
    "Qt 6.5.3 acquisition identity is missing")

set(vcpkg_ok FALSE)
if(presets MATCHES "x64-linux-chat-release" AND
   presets MATCHES "\\.ci/vcpkg_installed" AND
   triplet MATCHES "VCPKG_CMAKE_SYSTEM_NAME[ \t]+Linux" AND
   manifest MATCHES "fc3be1ebea7eaeb3071fe716ac65713af1f3a146")
    set(vcpkg_ok TRUE)
endif()
record_case("T10-LNX-06-vcpkg-identity" vcpkg_ok
    "vcpkg baseline, triplet, or run-owned install root drifted")

set(proto_ok FALSE)
if(root_cmake MATCHES "protobuf_generate|protoc" AND
   root_cmake MATCHES "proto/varify\\.proto" AND
   root_cmake MATCHES "proto/status\\.proto" AND
   root_cmake MATCHES "proto/chat\\.proto")
    set(proto_ok TRUE)
endif()
record_case("T10-LNX-07-canonical-proto" proto_ok
    "canonical proto generation is not wired")

set(node_ok FALSE)
if(runner MATCHES "npm ci" AND runner MATCHES "VarifyServer/server\\.js")
    set(node_ok TRUE)
endif()
record_case("T10-LNX-08-varify-startup" node_ok
    "Varify npm ci and production-main startup proof are missing")

set(startup_ok FALSE)
if(runner MATCHES "timeout" AND runner MATCHES "linux-preflight\\.json" AND
   preflight MATCHES "CHAT_LINUX_PREFLIGHT_SCHEMA_VERSION[ \t]+1")
    set(startup_ok TRUE)
endif()
record_case("T10-LNX-09-loader-startup" startup_ok
    "bounded loader/startup evidence is missing")

set(safety_ok TRUE)
string(TOLOWER "${workflow}" workflow_lower)
string(REGEX MATCHALL "GateServer/GateServer/CServer\\.cpp" gate_transport_occurrences "${root_cmake}")
list(LENGTH gate_transport_occurrences gate_transport_count)
if(workflow MATCHES "ubuntu-latest|self-hosted|docker[ \t]+(build|push)|continue-on-error" OR
   workflow_lower MATCHES "(password|token):[ \t]+[^$]" OR
   root_cmake MATCHES "FAKE|clearForTest" OR
   NOT gate_transport_count EQUAL 1)
    set(safety_ok FALSE)
endif()
record_case("T10-LNX-10-scope-safety" safety_ok
    "forbidden floating runner, application image, secret value, or fake seam found")

if(failure_count EQUAL 0)
    set(status "READY_FOR_HOSTED_PREFLIGHT")
    set(case_failure "")
    set(selector_exit 0)
else()
    set(status "BLOCKED")
    list(JOIN failures "; " failure_text)
    string(REPLACE "&" "&amp;" failure_xml "${failure_text}")
    string(REPLACE "<" "&lt;" failure_xml "${failure_xml}")
    string(REPLACE ">" "&gt;" failure_xml "${failure_xml}")
    set(case_failure "<failure message=\"LINUX_PREFLIGHT_BLOCKED\">${failure_xml}</failure>")
    set(selector_exit 1)
endif()

get_filename_component(junit_dir "${CHAT_JUNIT_PATH}" DIRECTORY)
get_filename_component(evidence_dir "${CHAT_EVIDENCE_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${junit_dir}" "${evidence_dir}")
file(WRITE "${CHAT_JUNIT_PATH}"
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<testsuite name=\"linux_preflight_contract\" tests=\"10\" failures=\"${failure_count}\" errors=\"0\" skipped=\"0\">"
    "${junit_cases}"
    "</testsuite>\n")

if(failure_count EQUAL 0)
    set(failure_json "[]")
else()
    string(REPLACE "\\" "\\\\" failure_json_text "${failure_text}")
    string(REPLACE "\"" "\\\"" failure_json_text "${failure_json_text}")
    set(failure_json "[\"${failure_json_text}\"]")
endif()
file(WRITE "${CHAT_EVIDENCE_PATH}"
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"status\": \"${status}\",\n"
    "  \"scope\": \"CI portability only; not Linux release support\",\n"
    "  \"failures\": ${failure_json}\n"
    "}\n")

if(CHAT_EXPECT STREQUAL "RED")
    if(selector_exit EQUAL 0)
        message(FATAL_ERROR "Expected RED but Linux preflight contract passed")
    endif()
    message(FATAL_ERROR "LINUX_PREFLIGHT_BLOCKED: ${failure_text}")
endif()

if(NOT selector_exit EQUAL 0)
    message(FATAL_ERROR "LINUX_PREFLIGHT_BLOCKED: ${failure_text}")
endif()
