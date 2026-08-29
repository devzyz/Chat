# Server RPC client and pool tests

## Production contract

Gate and Chat remote adapters share `common/grpc/GrpcClientRuntime.h`. The production Interface owns a bounded pool lease, applies a finite `ClientContext` deadline to every unary RPC, classifies transport failures internally, and maps them to the existing public `ErrorCodes::RPCFailed` contract without retrying.

Production defaults are:

- pool acquisition: 1000 ms;
- Gate-to-Status and Chat RPC: 3000 ms;
- Gate-to-Varify RPC: 15000 ms.

Configured values must be decimal milliseconds in the inclusive range 100..60000. Tests pass explicit 50..200 ms policies to keep failure paths deterministic. `StatusServer` currently owns no outbound client after the canonical-protocol migration, so it has no outbound deadline setting.

## Coverage

| Test IDs | Level | Report | Contract |
| --- | --- | --- | --- |
| T05-ROUTE-01..02 | Unit | `server_unit.xml` | Peer section names map to production runtime endpoints. |
| T08-RUNTIME-01..06 | Unit | `server_unit.xml` | Shared bounded pool, stable failure categories, and duration validation. |
| T08-GATE-POOL-01..06 | Unit | `server_unit.xml` | Actual Gate Varify/Status pools borrow/return, time out when exhausted, wake on close, remain closed, and accept returns after close safely. |
| T08-CHAT-POOL-01..06 | Unit | `server_unit.xml` | Actual Chat Status/peer pools obey the same lifecycle contract. |
| T08-GATE-RPC-01..04 | Integration | `server_integration.xml` | Real Gate production clients call dynamic loopback Varify/Status servers; deadline, unavailable, and peer-shutdown paths map to `RPCFailed`. |
| T08-CHAT-RPC-01..04 | Integration | `server_chat_grpc_integration.xml` | Real Chat production clients call dynamic loopback Status/Chat servers with the same failure coverage. |
| S01-GRPC-CFG-01 / S02-GRPC-CFG-01 | Integration | `server_integration.xml` | Real Chat/Gate executables reject out-of-range deadline/pool configuration before listening. |

The loopback fixtures bind `127.0.0.1:0`, use scoped server shutdown deadlines, expose no public network, and create no external Redis/MySQL/SMTP dependency. Gate and Chat production executables and their tests link the same `GateGrpcClients` / `ChatGrpcClients` static libraries; there is no copied parser, test-only switch, or private-method test seam.

## Execution

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
```

`RunServerTests` app-local deploys and executes all RPC reports, rejects a missing report or exact-count drift, and is part of the required develop CI lane.

## Known gaps

- There is no multi-process two-ChatServer business flow yet; cross-instance delivery and user-list semantics remain Phase 3 Business/E2E work.
- The current public error model intentionally collapses internal `PoolExhausted`, `Closed`, `DeadlineExceeded`, `Unavailable`, and `Cancelled` categories to `RPCFailed`. Changing that wire-visible behavior requires a reviewed contract change.
- No automatic retry is implemented or claimed.
