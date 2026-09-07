# Linux preflight contract

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

The selector first proves four mutations turn the same contract RED:

- a missing production target;
- a production source duplicated into another executable;
- vcpkg baseline drift;
- a broken Varify production-main invocation.

Any configure, compile, link, loader, startup, identity, timeout, or evidence
failure remains `LINUX_PREFLIGHT_BLOCKED`. The evidence scope is CI
portability; it is not application containerization or Linux release support.
