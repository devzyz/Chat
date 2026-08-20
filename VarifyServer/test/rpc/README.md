# Varify gRPC routing tests

## Production contract and coverage

The production `createServer(handler)` export in `VarifyServer/server.js` registers the generated `VarifyService` and routes `GetVarifyCode` to the supplied handler. The tests use the real generated client and `@grpc/grpc-js` transport over loopback.

| Test ID | Level | Path | Contract |
| --- | --- | --- | --- |
| T05-GRPC-01 | Component | normal/routing | An RPC reaches the registered handler and returns its response fields. |
| T05-GRPC-02 | Component | failure/lifecycle | A client deadline bounds a handler that never completes. |

The server binds `127.0.0.1:0`; the OS selects a dynamic port. Tests have five-second harness limits, calls use explicit deadlines, and `t.after` closes the client and force-shuts down only the server created by that test. Test identities use the reserved `.test` domain. No SMTP, Redis, public network, token, or repository credential is used.

## Local and CI execution

```powershell
cd VarifyServer
node --test test/rpc/rpc-routing.test.js
npm test
```

The existing `varify-release` CI job runs the explicit file list and writes `build/test-results/varify_unit.xml`; it uploads the report on success or failure and treats a missing report as an error.

## RED -> GREEN evidence

RED was recorded by temporarily expecting response code `WRONG`; `node --test test/rpc/rpc-routing.test.js` exited 1 and reported actual `TEST` versus expected `WRONG` in `loopback service routes GetVarifyCode to the registered handler`. The assertion was restored and the module passed 2/2.

## Known gaps

- This loopback verifies real Varify transport, but does not replace ChatServer/StatusServer routing coverage; the Server configuration mapping slice is in `tests/server/rpc`.
- The production mail/Redis handler has separate deterministic injected-dependency tests. Real Redis, SMTP, TLS, and process signal handling remain Integration gaps and are intentionally not invoked here.
