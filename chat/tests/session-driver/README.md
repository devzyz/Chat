# Production client process driver

Architecture / Integration. The GUI and separate `chat_e2e_client` test executable
both link `chat_client_login` (`ClientLoginFlow`), using the existing
`AuthFlowCoordinator`, `GateHttpTransport`, `TcpMgr` and `ChatTcpTransport`.
Both entry points use `ClientSession` and release the process-local singletons
before Qt shuts down. No production EXE test mode is added.

`ClientLoginFlow` owns the Gate-to-Chat orchestration formerly in `LoginDialog`,
preserving password transformation and production discovery. It cancels pending
requests, filters HTTP flow IDs, validates discovery fields and bounds the entire
login at ten seconds. Destroying it after authentication leaves the session to
its owner. The GUI retains input validation, error messages and navigation.

The controller creates a private `QLocalServer` (Unix local socket / Windows
named pipe), then launches `chat_e2e_client --control <endpoint>`. Within five
seconds the driver sends `{"event":"ready","format":1,"pid":...}`.
Newline-delimited JSON commands require strictly increasing positive integer
`id` values, at most 2^53-1:

- `snapshot`: safe active/uid/host/port state.
- `login`: requires `gate` (`http://127.0.0.1:<port>`), `email` and `password`.
  Runs shared production login; replies `authenticated` or `login-failed`.
  Credentials travel only in the private pipe and production transports;
  no Chat endpoint/token override is accepted.
- `stop`: cancels login, resets the session, replies `stopped`, flushes the
  channel and exits zero within one second.

Only one login may be pending. Input is capped at 8192 bytes, outbound queued
data at 64 KiB, and controller inactivity at 60 seconds. Invalid input, replayed
IDs, control loss and timeouts exit nonzero. The controller must own/reap its
child on failure. Snapshots never expose credentials, raw replies or containers.

| Test ID | CTest suffix under `session_driver.` | Contract |
| --- | --- | --- |
| E03-CONTRACT-01 | productionLoginKeepsAccountsAndEndpointsSeparate | Two processes authenticate through production HTTP/TCP against fixture peers; account/endpoint isolation survives one exit |
| E03-CONTRACT-02 | twoProcessesHaveIndependentLifetimes | Concurrent clients, independent command IDs and bounded stops |
| E03-CONTRACT-03 | malformedAndReplayedCommandsFailClosed | Malformed JSON, oversize input and repeated IDs rejected |
| E03-CONTRACT-04 | lostControllerTerminatesClient | Control loss causes bounded nonzero exit |
| E03-CONTRACT-05 | stopCancelsPendingProductionHttp | Stop cancels real pending HTTP and releases the socket |

From the repository root:

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
```

Focused configured-build entry:
`ctest --test-dir build/client -R '^session_driver\.' --output-on-failure`.
The five cases join `client_integration.xml` (28 cases; client total 54).
These process/loopback Integration results do not prove real Status selection
or five-service E2E. The fixed dataset and topology configuration live in
[tests/services](../../../tests/services/README.md). Message-model wiring,
five-service scenario orchestration and real-dependency hosted acceptance remain
subsequent 3D-00 work. Linux preflight also builds/runs these five cases and
preserves `linux_phase3d_client.xml`; this does not replace the planned 3D E2E gate.
