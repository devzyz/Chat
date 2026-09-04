# Integration host composition contract

`T09-HOST-01..06` defines the lifecycle boundary used by later Phase 3B transport suites. `integration::IntegrationHostFactory` accepts only numeric loopback endpoints, an absolute owned deadline, a host family, a factory that creates its concrete Phase 3A Server Modules, and a transport factory. Gate requires `GateRequest`, Status requires `StatusRouting`, and Chat requires both `LogicDispatcher` and `ChatSessionState`. The returned handle exposes the actual bound endpoint and protocol-ready probe, and owns bounded, observable, idempotent cleanup.

The contract target links the same `LogicDispatcher`, `ChatSessionState`, `GateRequest`, and `StatusRouting` libraries used by the formal executables. Its in-memory adapters construct those real Modules without Redis, MySQL, SMTP, public network access, or fixed shared ports. The controlled transport used by this host-contract test validates composition and lifecycle only; later 3B plans register the production Gate, Status, and Chat transports through the same factory seam and prove their protocol behavior.

The factory is a composition root only. It does not parse payloads, choose servers, map business errors, expose production containers, detach work, or compile a private copy of a production algorithm.

All six cases are registered in `ServerIntegrationTests.vcxproj` and emitted by the existing `server_integration.xml` owner. The report now contains 40 cases; the twelve-report regression manifest contains 238 cases. `CheckTestStructure` requires all four shared target references and rejects direct compilation of their production `.cpp` implementations.

Focused command:

```powershell
build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_HOST_* --gtest_output=xml:build/test-results/3b00_host_focused.xml
```
