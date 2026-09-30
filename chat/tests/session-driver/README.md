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
  `send` optionally accepts `copies: 2`: it immediately sends the identical
  production frame twice, appends one pending model row, and completes only after
  both responses. Any business failure is retained. E03-CONTRACT-01 checks two
  wire requests and one acknowledged model item.

Only one business command may be pending; snapshots and stop remain available.
An unexpected disconnect completes a pending TCP control command as `disconnected`
and releases its deadline. Production `TcpMgr` retains uncertain payloads; a new
public login for the same account retries their original bytes. The driver keeps
the same account's message model and clears it on account change. The existing
E03-CONTRACT-01 regression covers disconnect/relogin and a private chat absent
from the friend cache; production login renders a safe UID label for that chat.
TCP commands have a ten-second deadline, account HTTP requests five seconds.
`ClientSession` owns the ten-second heartbeat for both GUI and driver.
Input is capped at 8192 bytes, outbound queued
data at 64 KiB, and controller inactivity at 60 seconds. Invalid input, replayed
IDs, control loss and timeouts exit nonzero. The controller must own/reap its
child on failure. Snapshots never expose credentials, raw replies or containers.

The separate `--check-storage` packaging probe opens a temporary production
message database with plugin search restricted to the executable directory.
It requires the deployed SQLite plugin and never opens an existing account.

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

New send commands use MessageService and account/environment SQLite before TCP submission.
The loopback fixture supplies production ACK identity fields and recovery sync pages.
Explicit commands reusing an already committed UUID remain wire probes for server idempotency
and conflict rejection; they bypass only the local duplicate-send suppression. Recovery updates
existing pending model rows from stored facts, while the harness retains explicit legacy history
traversal for its separate paging contracts. QT_PLUGIN_PATH must include the kit SQL plugins.
The existing E03-CONTRACT-01 also reads legacy history after authenticated storage startup.
It interleaves a sync response and replays an unsolicited legacy response: only the explicitly
requested page may complete the history command and update its model cursor. Sync responses
remain owned by MessageService and legacy pages cannot advance its persisted sync cursor.

## Opt-in real acceptance controls

The existing driver also supports the manually selected real-dependency gate.
All commands require an authenticated production session; responses have
`status: completed`, `error`, and `result` (the existing send response remains
flat). No command inserts successful group or resource facts directly.

- `group-create`: `uuid`, `name`, `members`; `group-info`: `uuid`, `chatId`, `after`.
- `group-manage`: `uuid`, `chatId`, string `version`, `operation`, `params`
  (`members`, `target_uid`, or `name`). It persists the pending request before
  transmitting. Results preserve `local_save_failed` separately from server error.
- `group-state`: `chatId`; returns the production persisted membership snapshot.
- `group-send`: `uuid`, `chatId`, `text`, optional string `epoch`.
  New sends use the persistent outbox. Committed UUID probes replay the actual
  previously sent payload to exercise server duplicate/conflict behavior.
- `resource-send`: `uuid`, `chatId`, `toUid` (zero for groups), `descriptor`.
- `upload`: `path`; returns the real server descriptor. `download`: `descriptor`;
  returns actual file `path`, `sha256`, and `size` after production download/hash
  validation. Each transfer gets an isolated temporary cache; paths survive until
  the process exits, so the controller can decode downloaded media.
- `resource-probe`: `descriptor`; makes an independent authenticated real HTTP
  range GET and returns `httpStatus`. A network error is status zero and error -1,
  never an authorization denial. Use this alongside production transfer checks.
- `local-history`: `chatId`, string `before`; `search`: additionally `text`.
  Both return `result.messages` containing UUID, message/local IDs, sender, state
  and actual content hash. `sync` waits for a committed production sync event.
- `directory-find`: `kind` (`contacts` or `conversations`), `text`, `after`;
  returns `result.rows` from the complete account SQLite directory.
- `remark`: `uuid`, `toUid`, `backname`; `logout` resets the production session
  while keeping the controller alive for relogin/account isolation checks.

The test process can use `CHAT_RESOURCE_URL` for its isolated ResourceServer;
otherwise it reads the same `ResourceServer/Url` configuration as the GUI.
`group-manage` may set `localSaveFailure: true`: after the durable request has
been saved, a second SQLite connection acquires a real write lock before TCP
transmission. The production response handler must report its actual failed
write. `storage-unlock` releases the lock, allowing original-identity retry and
refresh recovery. This is confined to the test executable, not a production mode.

Extended commands have a 30-second deadline. These controls provide automated
production-client evidence, not manual Windows desktop acceptance. The existing
session-driver regression also exercises real SQLite history/search/directory
responses and logout; full group/resource outcomes require the real-dependency
scenario, not fixture-only test success.

The process regression also sends fixture TCP group responses through production
TcpMgr while an actual second SQLite connection holds a write lock. It verifies
that unrelated group sync failures cannot consume the pending management reply,
that server-success/local-save-failure remains explicit, and that unlocking then
retrying the identical management UUID completes without that local failure.
This proves the client persistence boundary only; server group transactions are
covered separately by the real-dependency run. A pre-send storage failure has no
request-scoped failure callback in the production API, so the driver fails at its
bounded deadline rather than misattributing a background directory failure.

CTest forces Qt diagnostic output to stderr for this suite, including on Windows
without an attached console. A missing control reply reports only the command
name/ID, elapsed time, process/socket state and buffered byte count. Request,
response and child-process log bodies are omitted because they can contain
credentials. The existing per-command deadlines remain unchanged.
