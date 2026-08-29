# Varify startup lifecycle tests

The production `startServer` interface owns bind validation and the start transition.

| Test ID | Level | Contract |
| --- | --- | --- |
| V07-START-01 | Unit | A bind error rejects and never calls `start`. |
| V07-START-02 | Unit | A zero/invalid bound port rejects and never calls `start`. |
| V07-START-03 | Unit | A successful bind starts once and returns the actual port. |
| V07-START-04 | Integration | Direct `node server.js` exits nonzero when port 50051 is occupied and does not expose configured credentials. |

Domain is Architecture. `startup-unit.test.js` contains the first three tests
using a fake server and capture logger; its report is `varify_unit.xml`.
`startup-integration.test.js` reserves port 50051 with a loopback fixture and
launches the real direct entry with an isolated temporary config plus synthetic
`CHAT_VARIFY_*` credentials; its report is `varify_integration.xml`. RED was a
direct-process exit status of zero after bind failure; GREEN is a generic error
plus nonzero exit. A successful direct-process readiness probe and signal-driven
graceful shutdown remain Integration gaps.
