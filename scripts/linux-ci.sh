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

run_contract() {
  local expectation="$1"
  cmake \
    -DCHAT_JUNIT_PATH="$junit_path" \
    -DCHAT_EVIDENCE_PATH="$evidence_path" \
    -DCHAT_EXPECT="$expectation" \
    -P "$repo_root/tests/build/linux_preflight_contract.cmake"
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
    run_contract GREEN
    ;;
esac

