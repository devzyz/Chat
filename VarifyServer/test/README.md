# VarifyServer tests

The directories follow production contracts. Domain and Level are independent
metadata; reports are split by Level.

| Directory | Domain | Level | Coverage |
| --- | --- | --- | --- |
| `protocol` | Foundation | Unit | Error constants and generated service descriptors. |
| `handler` | Business | Unit | Injected Redis/SMTP/UUID/logger behavior and log redaction. |
| `startup/startup-unit.test.js` | Architecture | Unit | Bind/start transitions through a fake server. |
| `config` | Foundation | Integration | Configuration loading in isolated child processes. |
| `rpc` | Architecture | Integration | Real dynamic-loopback gRPC registration and routing. |
| `startup/startup-integration.test.js` | Architecture | Integration | Direct-process bind failure on an occupied loopback port. |

From `VarifyServer`, run `npm run test:unit`, `npm run test:integration`, or
`npm test`. The repository entry point is `RunVarifyTests`. CI writes
`build/test-results/varify_unit.xml` and `varify_integration.xml`; a missing
report is an error. No test connects to Redis, SMTP, or a public network.

Successful default-process readiness, real adapters, signals, cross-language
C++ calls, and C++ deadline behavior remain gaps.
