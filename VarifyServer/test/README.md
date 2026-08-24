# VarifyServer tests

The Node 22 `node:test` suite is organized by production contract:

- `config`: configuration precedence and malformed input.
- `protocol`: error constants and generated service descriptors.
- `handler`: injected Redis/SMTP/UUID/logger behavior, including log redaction.
- `rpc`: real dynamic-loopback gRPC route registration.
- `startup`: bind/start lifecycle through the production `startServer` interface plus direct-process bind failure.

Run `npm test` in `VarifyServer` or `RunVarifyTests` from the repository root. Both execute the same explicit file list and write `build/test-results/varify_unit.xml` in CI. No test connects to Redis, SMTP, or a public network.

The real loopback protocol route and direct-process bind failure are Integration coverage. Successful default-process readiness, real adapters, signals, cross-language C++ calls, and C++ deadline behavior remain gaps.
