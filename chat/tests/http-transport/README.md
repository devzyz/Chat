# Gate HTTP Transport Integration Contracts

`Q04-HTTP-01..10` exercise the production `GateHttpTransport` public interface over numeric loopback with the real Qt `QNetworkAccessManager` stack.

The module covers successful POST identity propagation, deterministic connection refusal, a silent-peer deadline, malformed JSON, the exact 8 KiB response boundary and one-byte-over rejection, mid-response close, explicit cancellation, duplicate terminal suppression, stale-flow suppression, and QObject/socket cleanup.

Tests never receive a `QNetworkReply`, network manager, handler map, or reset hook. The GUI `HttpMgr` and this test executable both link `chat_gate_http_transport`; the transport returns stable flow/request/module identities that the existing authentication path forwards to `AuthFlowCoordinator`.

The focused selector is:

```powershell
ctest --test-dir build/windows-client/Release -R "http_transport" --output-on-failure
```

All ten cases are emitted to `client_integration.xml`. Together with seven new Server Integration cases, Plan 3B-02 raises `server_integration.xml` to 59 cases, Server to 191, Qt to 34, and the complete manifest to 13 reports / 267 cases.

The meaningful Task 2 mutation emitted a second terminal completion. `explicitCancelHasExactlyOneTerminalOutcome` failed with two results instead of one, and the restored SHA-256 for `gatehttptransport.cpp` is `1C893D108A2DB0514E951C0363DCFB824CEF044A116BEACF72FAD93FEB9AD6AC`.
