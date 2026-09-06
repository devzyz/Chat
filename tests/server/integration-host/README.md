# Integration host composition contract

`T09-HOST-01..06` defines the lifecycle boundary used by later Phase 3B transport suites. `integration::IntegrationHostFactory` accepts only numeric loopback endpoints, an absolute owned deadline, a host family, a factory that creates its concrete Phase 3A Server Modules, and a transport factory. Gate requires `GateRequest`, Status requires `StatusRouting`, and Chat requires both `LogicDispatcher` and `ChatSessionState`. The returned handle exposes the actual bound endpoint and protocol-ready probe, and owns bounded, observable, idempotent cleanup.

The contract target links the same `LogicDispatcher`, `ChatSessionState`, `GateRequest`, and `StatusRouting` libraries used by the formal executables. Its in-memory adapters construct those real Modules without Redis, MySQL, SMTP, public network access, or fixed shared ports. The controlled transport used by this host-contract test validates composition and lifecycle only; later 3B plans register the production Gate, Status, and Chat transports through the same factory seam and prove their protocol behavior.

The factory is a composition root only. It does not parse payloads, choose servers, map business errors, expose production containers, detach work, or compile a private copy of a production algorithm.

The six host cases, seven `T09-GHTTP-01..07` cases, twelve `T09-SGRPC-01..12` cases, sixteen `T09-CTCP-01..16` cases, and six actual `T09-COMP-01..06` cases are registered in `ServerIntegrationTests.vcxproj` and emitted by the existing `server_integration.xml` owner. The report now contains 93 cases; the thirteen-report regression manifest contains 313 cases while preserving the original twelve-report/232-testcase floor. `CheckTestStructure` requires all shared production target references and rejects direct compilation of production `.cpp` implementations.

The Gate HTTP cases cross production Beast parsing and lifecycle code through the shared `GateTransport.vcxproj`. They prove actual numeric-loopback publication, all four POST routes delegating to the same Phase 3A `GateRequest`, fragmented exact-limit acceptance, max+1 rejection, malformed request rejection, interrupted connection cleanup, and idempotent stop. No Redis, MySQL, gRPC service, public endpoint, or test-only production switch is used.

`T09-SGRPC-01..12` crosses the generated Status stub and production gRPC service through the shared `StatusTransport.vcxproj` into the Phase 3A `StatusRouting` Module. Core contracts cover readiness, assignment, login, fail-closed empty routing, the maximum protobuf integer boundary, and sanitized business errors. Fault contracts cover deadline expiry, explicit cancellation, refused connection, shutdown during a call, late completion across host generations, occupied-port rejection, idempotent stop, restart, and complete port/server resource release. Delay is scheduled only in the in-memory `StatusStore` Adapter with condition variables; the suite adds no test RPC, fixed port, public network access, or sleep.

`T09-CTCP-01..16` sends raw loopback bytes through production `chat_transport::CServer`, `CSession`, `ChatFrameCodec`, and the injected Phase 3A `LogicDispatcher`, all owned by the shared `ChatTransport.vcxproj`. Stream contracts cover split headers and bodies, adjacent and zero-body frames, the exact maximum and max+1 boundaries, malformed input, and interrupted reads/writes. Fault contracts cover refusal/read deadlines, deterministic partial-write backpressure, occupied ports, pending-accept cancellation, idempotent stop, and complete session/thread/socket/server/port release. The suite uses only run-owned numeric loopback and in-memory adapters; it adds no Redis, MySQL, Status, peer RPC, fixed port, public network, or sleep dependency.

`T09-COMP-01..06` black-boxes the three formal executables without adding a fake dependency mode. Gate and Status each prove explicit configuration precedence, fail-closed validation, real HTTP/gRPC protocol readiness, documented graceful signalling, bounded exit, and listener release. Chat proves explicit configuration precedence, then records the intentional G-015/Phase 3C boundary: its formal ready path requires disposable real dependencies, so the 3B contract observes no synthetic ready result, performs a bounded owned stop, verifies stable nonzero termination and pipe/port/temp cleanup, and rejects public-endpoint or synthetic-credential evidence. The frozen `T09-COMP-07..08` slots remain structure-only planned identifiers rather than fabricated JUnit cases.

`CheckTestStructure` ties each formal executable and `ServerIntegrationTests` to the same Gate/Status/Chat transport target and Phase 3A business target. It also requires real production Adapter selection, exactly six actual T09-COMP cases, and rejects formal fake-dependency flags, test macros, test-only Interfaces, or private copies of production sources.

Focused command:

```powershell
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_HOST_* --gtest_output=xml:build/test-results/3b00_host_focused.xml
```

Gate HTTP focused command:

```powershell
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_GHTTP_Core* --gtest_output=xml:build/test-results/3b02_gate_http_focused.xml
```

Status gRPC focused commands:

```powershell
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_SGRPC_Core* --gtest_output=xml:build/test-results/3b03_status_core.xml
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_SGRPC_Fault* --gtest_output=xml:build/test-results/3b03_status_faults.xml
```

Chat TCP focused command:

```powershell
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_CTCP_Stream.* --gtest_output=xml:build/test-results/3b04_chat_stream.xml
```

Formal composition focused command:

```powershell
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_COMP_* --gtest_output=xml:build/test-results/3b05_composition.xml
```
