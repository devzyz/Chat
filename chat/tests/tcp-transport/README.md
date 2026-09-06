# Chat TCP Transport Integration Contracts

`Q04-TCP-01..12` exercise the public `ChatTcpTransport` seam over run-owned numeric loopback with real `QTcpSocket` peers and the production `TcpFrameDecoder`.

The cases cover connection and flow identity, framed sends, fragmented and coalesced reads, the exact 2 KiB body boundary, malformed oversized input, bounded refusal, finite write deadline, peer close during queued writes, reset of a half frame, stale-generation retry suppression, one terminal outcome, and complete connection release.

Tests never receive the transport socket, decoder, write queue, timer, or handler map. `TcpMgr` owns a `ChatTcpTransport` and retains only business message handlers and pending acknowledgement identity.

The focused selector is:

```powershell
ctest --test-dir build/windows-client/Release -R "tcp_transport" --output-on-failure
```

The twelve cases are part of the 22-case `client_integration.xml` report and the 307-case thirteen-report regression manifest. The stale-generation identity mutation is owned by `Q04-TCP-11`; it fails when terminal identity is cleared before publication and passes only after restoring the production generation/flow snapshot.
