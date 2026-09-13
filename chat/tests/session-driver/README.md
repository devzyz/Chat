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
- `snapshot` with `otherUid`: production UserMgr application/friend state and
  private chat ID, without profile text or credentials.
- `apply` / `accept`: `toUid`, `description`, `backname`; uses the same request
  builders as ApplyFriendDialog/AuthFriendDialog. Acceptance requires an actual
  application received by the production TCP handler or loaded at login.
  Missing application returns `no-application`; malformed commands fail closed.
  The existing E03-CONTRACT-01 process case verifies authenticated request identity,
  notification storage, acceptance, safe snapshots and missing-application rejection.
- `snapshot` with `chatId`: at most 128 model rows with UUID, server ID,
  sender, delivery state and text SHA-256; includes history cursor/more state.
- `verify`: `gate`, `email`; `register`: also `name`, `password`, `code`.
  Uses production Gate HTTP transport and registration password transformation.
- `login`: requires `gate` (`http://127.0.0.1:<port>`), `email` and `password`.
  Runs shared production login; replies `authenticated` or `login-failed`.
  Credentials travel only in the private pipe and production transports;
  no Chat endpoint/token override is accepted.
- `stop`: cancels login, resets the session, replies `stopped`, flushes the
  channel and exits zero within one second.
- `create`: `toUid`; `send`: `chatId`, `toUid`, `uuid`, `text`;
  `history`: `chatId`, `cursor` (nonnegative decimal string).
  Uses the GUI's shared request builders, production TCP handlers, DTO conversion
  and `MessageModelStore` history/ACK/failure operations. Completion follows
  production response handling; transport generation filtering remains in TcpMgr.

Only one business command may be pending; snapshots and stop remain available.
TCP commands have a ten-second deadline, account HTTP requests five seconds.
`ClientSession` owns the ten-second heartbeat for both GUI and driver.
Input is capped at 8192 bytes, outbound queued
data at 64 KiB, and controller inactivity at 60 seconds. Invalid input, replayed
IDs, control loss and timeouts exit nonzero. The controller must own/reap its
child on failure. Snapshots never expose credentials, raw replies or containers.

| Test ID | CTest suffix under `session_driver.` | Contract |
| --- | --- | --- |
| E03-CONTRACT-01 | productionLoginKeepsAccountsAndEndpointsSeparate | Two processes authenticate through production HTTP/TCP against fixture peers; send/ACK updates the model, both heartbeat, and account/endpoint isolation survives one exit |
| E03-CONTRACT-02 | twoProcessesHaveIndependentLifetimes | Concurrent clients, public account HTTP requests, independent command IDs and bounded stops |
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
[tests/services](../../../tests/services/README.md). The `3D-00` selector runs
the five-service scenario separately against real dependencies. Linux preflight builds/runs these five cases and
preserves `linux_phase3d_client.xml`; this does not replace the planned 3D E2E gate.
