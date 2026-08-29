# Client network-state tests

## Production contract and coverage

`chat/tcpframedecoder.*` is the non-Widget Qt Core decoder used directly by `TcpMgr::readyRead`. It implements the existing wire contract: two big-endian 16-bit header fields followed by exactly the declared body bytes, retaining incomplete input between reads.

Domain is Foundation and Level is Unit.

| Test ID | Level | Path | Contract |
| --- | --- | --- | --- |
| T07-FRM-01 | Unit | boundary/state | A split header is retained and later decoded in network byte order. |
| T07-FRM-02 | Unit | boundary/state | A split body is retained until complete. |
| T07-FRM-03 | Unit | normal/boundary | Adjacent frames, including a zero-length body, are emitted in order without residue. |
| T07-FRM-04 | Unit | lifecycle | Reset discards a partial old-connection frame before a fresh connection frame is decoded. |

This deterministic seam is justified by the state machine formerly embedded in the socket callback. It requires only `QCoreApplication`/Qt Core: no display, singleton user state, socket, fixed port, wait, public network, or credential. Values clean up at scope exit.

## Local and CI execution

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest --test-dir build/windows-client/Release -C Release --output-on-failure
```

`chat/CMakeLists.txt` registers `network_state_tests`. Both the application and
the test link `chat_network_core`, so the regression test executes the same
compiled production module. The existing `client-release` job exports JUnit
XML to `build/test-results/client_unit.xml`, uploads it even after failure, and
treats a missing report as an error.

## RED -> GREEN evidence

RED was recorded by temporarily expecting message id `0x1235` from a `0x1234` frame. The rebuilt executable exited 1 with `message id was not decoded in network order`. The assertion was restored, rebuilt, and exited 0; the unified client suite then passes both registered executables.

## Known gaps

- No maximum client frame length is implemented, so this module does not invent a rejection policy. Malformed/oversized-frame handling remains a production-policy gap.
- Real `QTcpSocket` loopback, connection/error timing, and `HttpMgr` outcomes remain outside this Unit module. Authenticated state and pending-batch reset are owned by the adjacent `session-reset` Component module.
- The earlier `message-model` suite owns model/delegate behavior and is not repeated here.
