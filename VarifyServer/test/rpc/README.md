# Varify gRPC routing test

`T05-GRPC-01` is a loopback Integration test of the production `createServer(handler)` registration path and generated Varify client. It binds `127.0.0.1:0`, uses a bounded call, and registers `t.after` cleanup for the client and server. It never loads SMTP or Redis.

Run `node --test test/rpc/rpc-routing.test.js`, `npm test`, or `RunVarifyTests`; CI writes `varify_unit.xml`.

The former `T05-GRPC-02` was removed because it proved only `@grpc/grpc-js` deadline behavior, not a project client adapter. C++ client deadline behavior remains an explicit gap. Real Redis, SMTP, TLS, process signals, and cross-language RPC are also Integration gaps.
