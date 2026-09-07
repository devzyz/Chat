#!/usr/bin/env bash
set -euo pipefail

phase=""
configuration=""
selector=""
expect_red=0
list_only=0

while (($#)); do
  case "$1" in
    --phase) phase="${2:-}"; shift 2 ;;
    --configuration) configuration="${2:-}"; shift 2 ;;
    --selector) selector="${2:-}"; shift 2 ;;
    --expect-red) expect_red=1; shift ;;
    --list-only) list_only=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 64 ;;
  esac
done

if [[ "$phase" != "3C" || "$configuration" != "Release" ]]; then
  echo "--phase 3C and --configuration Release are required" >&2
  exit 64
fi

declare -A selectors=(
  [3C-00-T1]=preflight_contract_red
  [3C-00-T2]=preflight_build_green
)

if ((list_only)); then
  printf '%s\n' "${!selectors[@]}" | sort
  exit 0
fi

if [[ -z "$selector" ]]; then
  echo "full Phase 3C lane is reserved for 3C-09-T2" >&2
  exit 65
fi
if [[ -z "${selectors[$selector]+x}" ]]; then
  echo "selector is not registered: $selector" >&2
  exit 64
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
evidence_root="${CHAT_EVIDENCE_ROOT:-${repo_root}/out/phase3c/preflight}"
junit_path="${evidence_root}/junit/linux_build_proof.xml"
evidence_path="${evidence_root}/linux-preflight.json"
mkdir -p "$(dirname "$junit_path")"
current_step="contract"
runtime_complete=0

write_runtime_evidence() {
  local status="$1"
  local diagnostic="$2"
  local compiler="${3:-unobserved}"
  local cmake_version="${4:-unobserved}"
  local qt_version="${5:-unobserved}"
  local node_version="${6:-unobserved}"
  local vcpkg_commit="${7:-unobserved}"
  printf '%s\n' \
    '{' \
    '  "schema_version": 1,' \
    "  \"status\": \"${status}\"," \
    '  "scope": "CI portability only; not application containerization or Linux release support",' \
    '  "runner": "ubuntu-24.04",' \
    "  \"compiler\": \"${compiler}\"," \
    "  \"cmake\": \"${cmake_version}\"," \
    "  \"qt\": \"${qt_version}\"," \
    "  \"node\": \"${node_version}\"," \
    "  \"vcpkg_baseline\": \"${vcpkg_commit}\"," \
    '  "triplet": "x64-linux-chat-release",' \
    '  "targets": ["GateServer", "StatusServer", "ChatServer", "chat_network_core", "chat_message_model", "chat_session_core", "chat_protocol_cpp"],' \
    "  \"diagnostic\": \"${diagnostic}\"" \
    '}' > "$evidence_path"
}

on_exit() {
  local exit_code=$?
  if ((exit_code != 0 && runtime_complete == 0)); then
    write_runtime_evidence "LINUX_PREFLIGHT_BLOCKED" "${current_step}"
  fi
}
trap on_exit EXIT
# GNU timeout signals this shell and its children as a process group. Exit
# non-zero after child termination so the EXIT trap preserves blocked evidence.
trap 'exit 143' TERM

run_contract() {
  local expectation="$1"
  cmake \
    -DCHAT_JUNIT_PATH="$junit_path" \
    -DCHAT_EVIDENCE_PATH="$evidence_path" \
    -DCHAT_EXPECT="$expectation" \
    -P "$repo_root/tests/build/linux_preflight_contract.cmake"
}

expect_mutation_red() {
  local mutation_name="$1"
  local mutation_root="$2"
  set +e
  local output
  output="$(cmake \
    -DCHAT_REPO_ROOT="$mutation_root" \
    -DCHAT_JUNIT_PATH="$mutation_root/junit.xml" \
    -DCHAT_EVIDENCE_PATH="$mutation_root/evidence.json" \
    -DCHAT_EXPECT=GREEN \
    -P "$repo_root/tests/build/linux_preflight_contract.cmake" 2>&1)"
  local result=$?
  set -e
  if ((result == 0)) || [[ "$output" != *"LINUX_PREFLIGHT_BLOCKED"* ]]; then
    printf '%s\n' "$output" >&2
    echo "mutation did not make the contract RED: $mutation_name" >&2
    return 1
  fi
  printf '%s\n' "mutation RED: $mutation_name"
}

copy_contract_inputs() {
  local destination="$1"
  mkdir -p \
    "$destination/cmake" \
    "$destination/triplets" \
    "$destination/scripts" \
    "$destination/.github/workflows"
  cp "$repo_root/CMakeLists.txt" "$destination/CMakeLists.txt"
  cp "$repo_root/CMakePresets.json" "$destination/CMakePresets.json"
  cp "$repo_root/vcpkg.json" "$destination/vcpkg.json"
  cp "$repo_root/cmake/LinuxPreflight.cmake" "$destination/cmake/LinuxPreflight.cmake"
  cp "$repo_root/triplets/x64-linux-chat-release.cmake" "$destination/triplets/x64-linux-chat-release.cmake"
  cp "$repo_root/scripts/linux-ci.sh" "$destination/scripts/linux-ci.sh"
  cp "$repo_root/.github/workflows/linux-ci.yml" "$destination/.github/workflows/linux-ci.yml"
}

run_contract_mutations() {
  local mutation_parent
  mutation_parent="$(mktemp -d "${RUNNER_TEMP:-/tmp}/chat-preflight-mutations.XXXXXX")"

  local rpc_root="$mutation_parent/missing-rpc-prerequisite"
  copy_contract_inputs "$rpc_root"
  sed -i '/sudo apt-get install --yes --no-install-recommends libtirpc-dev/d' \
    "$rpc_root/.github/workflows/linux-ci.yml"
  expect_mutation_red "missing MySQL native RPC prerequisite" "$rpc_root"

  local target_root="$mutation_parent/missing-target"
  copy_contract_inputs "$target_root"
  sed -i 's/CHAT_LINUX_PRODUCTION_TARGETS_READY ON/CHAT_LINUX_PRODUCTION_TARGETS_READY OFF/' \
    "$target_root/CMakeLists.txt"
  expect_mutation_red "missing production target" "$target_root"

  local duplicate_root="$mutation_parent/duplicate-source"
  copy_contract_inputs "$duplicate_root"
  printf '%s\n' 'add_executable(preflight_duplicate GateServer/GateServer/CServer.cpp)' \
    >> "$duplicate_root/CMakeLists.txt"
  expect_mutation_red "duplicate production source" "$duplicate_root"

  local baseline_root="$mutation_parent/baseline-drift"
  copy_contract_inputs "$baseline_root"
  sed -i 's/fc3be1ebea7eaeb3071fe716ac65713af1f3a146/0000000000000000000000000000000000000000/' \
    "$baseline_root/vcpkg.json"
  expect_mutation_red "vcpkg baseline drift" "$baseline_root"

  local host_triplet_root="$mutation_parent/host-triplet-missing"
  copy_contract_inputs "$host_triplet_root"
  sed -i '/"VCPKG_HOST_TRIPLET"/d' "$host_triplet_root/CMakePresets.json"
  expect_mutation_red "missing native Release host triplet" "$host_triplet_root"

  local mysql_static_missing_root="$mutation_parent/mysql-static-missing"
  copy_contract_inputs "$mysql_static_missing_root"
  sed -i '/if(PORT STREQUAL "mysql-connector-cpp"/,/endif()/d' \
    "$mysql_static_missing_root/triplets/x64-linux-chat-release.cmake"
  expect_mutation_red "missing per-port MySQL static linkage" "$mysql_static_missing_root"

  local mysql_static_broad_root="$mutation_parent/mysql-static-broad"
  copy_contract_inputs "$mysql_static_broad_root"
  sed -i 's/OR PORT STREQUAL "libmysql")/OR PORT STREQUAL "libmysql" OR PORT STREQUAL "hiredis")/' \
    "$mysql_static_broad_root/triplets/x64-linux-chat-release.cmake"
  expect_mutation_red "broadened per-port MySQL static linkage" "$mysql_static_broad_root"

  local vcpkg_depth_missing_root="$mutation_parent/vcpkg-depth-missing"
  copy_contract_inputs "$vcpkg_depth_missing_root"
  sed -i '/fetch-depth: 0/d' \
    "$vcpkg_depth_missing_root/.github/workflows/linux-ci.yml"
  expect_mutation_red "missing full vcpkg checkout depth" "$vcpkg_depth_missing_root"

  local vcpkg_depth_shallow_root="$mutation_parent/vcpkg-depth-shallow"
  copy_contract_inputs "$vcpkg_depth_shallow_root"
  sed -i 's/fetch-depth: 0/fetch-depth: 1/' \
    "$vcpkg_depth_shallow_root/.github/workflows/linux-ci.yml"
  expect_mutation_red "shallow vcpkg checkout depth" "$vcpkg_depth_shallow_root"

  local cmake_action_root="$mutation_parent/cmake-action-missing"
  copy_contract_inputs "$cmake_action_root"
  sed -i 's#lukka/get-cmake@fffaaafeea488556c2c12dad60690008bc1caacb#lukka/missing-cmake-action@fffaaafeea488556c2c12dad60690008bc1caacb#' \
    "$cmake_action_root/.github/workflows/linux-ci.yml"
  expect_mutation_red "missing locked CMake acquisition action" "$cmake_action_root"

  local cmake_ref_root="$mutation_parent/cmake-action-floating-ref"
  copy_contract_inputs "$cmake_ref_root"
  sed -i 's#lukka/get-cmake@fffaaafeea488556c2c12dad60690008bc1caacb#lukka/get-cmake@v4.4.2#' \
    "$cmake_ref_root/.github/workflows/linux-ci.yml"
  expect_mutation_red "floating CMake acquisition action ref" "$cmake_ref_root"

  local cmake_identity_root="$mutation_parent/cmake-identity-drift"
  copy_contract_inputs "$cmake_identity_root"
  sed -i 's/cmakeVersion: "3.28.3"/cmakeVersion: "3.31.6"/' \
    "$cmake_identity_root/.github/workflows/linux-ci.yml"
  expect_mutation_red "CMake acquisition identity drift" "$cmake_identity_root"

  local startup_root="$mutation_parent/startup-break"
  copy_contract_inputs "$startup_root"
  sed -i 's#VarifyServer/server.js#VarifyServer/missing-main.js#g' \
    "$startup_root/scripts/linux-ci.sh"
  expect_mutation_red "broken Varify startup invocation" "$startup_root"

  rm -rf "$mutation_parent"
}

assert_exact_identity() {
  local name="$1"
  local actual="$2"
  local expected="$3"
  if [[ "$actual" != "$expected" ]]; then
    echo "LINUX_PREFLIGHT_BLOCKED: $name identity mismatch; expected $expected, got $actual" >&2
    return 1
  fi
}

run_cpp_startup_probe() {
  local name="$1"
  local executable="$2"
  local log_path="$evidence_root/${name}.startup.log"
  set +e
  timeout --signal=TERM --kill-after=5s 10s "$executable" --preflight-loader-probe \
    >"$log_path" 2>&1
  local result=$?
  set -e
  if ((result == 0 || result == 124 || result == 126 || result == 127)) || \
     ! grep -Fq 'Usage:' "$log_path"; then
    echo "LINUX_PREFLIGHT_BLOCKED: $name loader/startup probe failed" >&2
    return 1
  fi
}

run_varify_startup_probe() {
  local log_path="$evidence_root/VarifyServer.startup.log"
  set +e
  (
    cd "$repo_root/VarifyServer"
    timeout --signal=TERM --kill-after=5s 10s node server.js
  ) >"$log_path" 2>&1
  local result=$?
  set -e
  if ((result != 124)) || ! grep -Fq 'grpc server started on port' "$log_path"; then
    echo "LINUX_PREFLIGHT_BLOCKED: Varify production-main startup probe failed" >&2
    return 1
  fi
}

case "$selector" in
  3C-00-T1)
    if ((expect_red == 0)); then
      echo "3C-00-T1 is an expected-RED selector; pass --expect-red" >&2
      exit 64
    fi
    set +e
    contract_output="$(run_contract RED 2>&1)"
    contract_status=$?
    set -e
    if ((contract_status == 0)) || [[ "$contract_output" != *"LINUX_PREFLIGHT_BLOCKED"* ]]; then
      printf '%s\n' "$contract_output" >&2
      echo "expected named RED was not observed" >&2
      exit 1
    fi
    printf '%s\n' "3C-00-T1 expected RED: LINUX_PREFLIGHT_BLOCKED"
    ;;
  3C-00-T2)
    if ((expect_red)); then
      echo "3C-00-T2 is a GREEN selector" >&2
      exit 64
    fi
    current_step="hosted-runner-identity"
    if [[ "$(uname -s)" != "Linux" || -z "${GITHUB_ACTIONS:-}" ]]; then
      echo "LINUX_PREFLIGHT_BLOCKED: authoritative hosted Ubuntu runner is required" >&2
      exit 1
    fi

    current_step="contract-static"
    run_contract GREEN
    current_step="contract-mutations"
    run_contract_mutations

    current_step="tool-identities"
    compiler_version="$(gcc -dumpfullversion)"
    cmake_version="$(cmake --version | awk 'NR == 1 {print $3}')"
    qt_version="$(qmake -query QT_VERSION)"
    node_version="$(node --version | sed 's/^v//')"
    vcpkg_commit="$(git -C "$VCPKG_ROOT" rev-parse HEAD)"
    assert_exact_identity compiler "$compiler_version" "${CHAT_EXPECTED_GNU_VERSION:-13.3.0}"
    assert_exact_identity CMake "$cmake_version" "${CHAT_EXPECTED_CMAKE_VERSION:-3.28.3}"
    assert_exact_identity Qt "$qt_version" "${CHAT_EXPECTED_QT_VERSION:-6.5.3}"
    assert_exact_identity Node "$node_version" "${CHAT_EXPECTED_NODE_VERSION:-22.18.0}"
    assert_exact_identity vcpkg "$vcpkg_commit" fc3be1ebea7eaeb3071fe716ac65713af1f3a146

    current_step="cmake-configure"
    cmake --preset linux-x64-release
    current_step="cmake-compile-link"
    cmake --build --preset linux-x64-release --target \
      GateServer StatusServer ChatServer \
      chat_network_core chat_message_model chat_session_core chat_protocol_cpp

    current_step="varify-npm-ci"
    (cd "$repo_root/VarifyServer" && npm ci --ignore-scripts)

    current_step="loader-startup"
    binary_root="$repo_root/out/build/linux-x64-release/bin"
    run_cpp_startup_probe GateServer "$binary_root/GateServer"
    run_cpp_startup_probe StatusServer "$binary_root/StatusServer"
    run_cpp_startup_probe ChatServer "$binary_root/ChatServer"
    run_varify_startup_probe

    current_step="evidence-finalize"
    write_runtime_evidence \
      "PASS" \
      "configure-compile-link-loader-startup" \
      "GNU-${compiler_version}" \
      "$cmake_version" \
      "$qt_version" \
      "$node_version" \
      "$vcpkg_commit"
    runtime_complete=1
    ;;
esac
