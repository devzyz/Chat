# Server RPC routing tests

## Production contract and coverage

`ChatServer/ChatServer/PeerServerRouting.*` is the deterministic extraction used by `ChatGrpcClient.cpp` to translate configured peer section names through `ConfigMgr` into runtime server-name to host/port routes. Host and port remain separate, preserving the production client's existing endpoint construction semantics.

| Test ID | Level | Path | Contract |
| --- | --- | --- | --- |
| T05-ROUTE-01 | Unit | normal | Multiple configured sections map runtime names to their endpoints. |
| T05-ROUTE-02 | Unit | failure/boundary | A section with no runtime `Name` does not create an unusable route. |

The lookup callback is the minimal seam around the existing configuration source. Tests use only local maps and destroy all state at scope exit; there is no gRPC channel, server process, port, wait, credential, or network request.

## Local and CI execution

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=PeerServerRoutingTests.*
```

The existing `servers-release` CI job runs these tests and uploads `build/test-results/server_unit.xml` even after failures; a missing report is an error.

## RED -> GREEN evidence

The shared Phase 2 RPC transport RED gate is recorded in `VarifyServer/test/rpc/README.md`. This C++ routing slice was separately executed GREEN as part of the nine new Server tests and final full Server suite.

## Known gaps (partial coverage)

- This is partial RPC coverage. A two-ChatServer Integration environment is not available, so channel readiness, unavailable peers, cross-instance delivery, empty user lists, and cleanup are not claimed.
- Current C++ unary calls do not set deadlines and clients/stubs are not injectable. Those behaviors need a production contract/seam before required tests are added.
- Protobuf descriptor and message roundtrip behavior is already covered in `tests/server/protocol` and is not duplicated here.
