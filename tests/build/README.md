# Linux preflight contract

Production source ownership is shared with the explicit MSBuild project entries
through `cmake/ServerSourceOwnership.cmake`. Linux builds the same GateTransport,
GateRequest, GateGrpcClients, StatusTransport, StatusRouting, ChatTransport,
ChatSessionState, LogicDispatcher and ChatGrpcClients libraries used upstream;
formal servers and IntegrationHost consumers link those targets. Generated proto
remains owned by `chat_protocol_cpp`. Conditional or property-expanded source
entries fail configuration instead of silently dropping a source. Run the
central check with `cmake -P tests/build/server_source_ownership.cmake`.

The [process module README](../server/process-harness/README.md) owns 3C-01
commands, registration and the Linux-only evidence boundary.

Plan 3C-00 owns the public selector:

```text
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-00-T2
```

The selector is authoritative only on the pinned GitHub-hosted
`ubuntu-24.04` job. It uses the repository baseline and
`x64-linux-chat-release` triplet with a run-owned `.ci/vcpkg_installed`
directory. It never uses or modifies either DG-25 local Windows dependency
tree.

`linux_preflight_contract.cmake` writes ten stable `T10-LNX-01..10` cases to
the exact `CHAT_JUNIT_PATH` and writes machine-readable status to the exact
`CHAT_EVIDENCE_PATH`. A local/static success is
`READY_FOR_HOSTED_PREFLIGHT`; only the hosted selector may replace that with
`PASS` after compiler, CMake, Qt, vcpkg, canonical proto, compile/link, and
bounded production-main startup checks complete.

`T10-LNX-10-scope-safety` checks workflow password/token values after trimming
whitespace and scalar quotes. Only complete GitHub expressions and the exact
`MYSQL_ALLOW_EMPTY_PASSWORD: "yes"` disposable bootstrap flag are accepted;
literal credentials, other password keys and arbitrary flag values remain
blocked. The same scanner runs positive and negative synthetic regression cases
inside this contract, including multi-space expressions and a bootstrap flag
next to a literal root password. Run the ten static cases without restoring
dependencies:

```sh
cmake -DCHAT_EXPECT=GREEN -DCHAT_JUNIT_PATH=out/phase3c/contract.xml -DCHAT_EVIDENCE_PATH=out/phase3c/contract.json -P tests/build/linux_preflight_contract.cmake
```

The root CMake project resolves Qt Core 6.5.3 explicitly in its own directory
scope before checking the Qt identity. Qt discovery inside the `chat/` child
directory does not export `Qt6Core_VERSION` to the root. A missing or different
version remains a configuration failure; it is never inferred from the
requested version or replaced by a default.

The Varify loader probe runs the production `server.js` with
`fixtures/varify-loader.json`, loopback endpoints and synthetic non-secret
credential values scoped to its child process. It sends no mail or business
requests and proves only that the production listener starts and remains alive
until the bounded timeout. Redis/SMTP readiness belongs to later integration
plans. Missing configuration, early exit, bind failure or absent startup output
still fails this probe.

The selector first proves four mutations turn the same contract RED:

- a missing production target;
- a production source duplicated into another executable;
- vcpkg baseline drift;
- a broken Varify production-main invocation.

Any configure, compile, link, loader, startup, identity, timeout, or evidence
failure remains `LINUX_PREFLIGHT_BLOCKED`. The evidence scope is CI
portability; it is not application containerization or Linux release support.
