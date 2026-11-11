# Server RPC client and pool tests

## Production contract

Gate and Chat remote adapters share `common/grpc/GrpcClientRuntime.h`. The production Interface owns a bounded pool lease, applies a finite `ClientContext` deadline to every unary RPC, classifies transport failures internally, and maps them to the existing public `ErrorCodes::RPCFailed` contract without retrying.

Production defaults are:

- pool acquisition: 1000 ms;
- Chat peer RPC: 3000 ms;
- Gate-to-Varify RPC: 15000 ms.

Configured values must be decimal milliseconds in the inclusive range 100..60000. Tests pass explicit 50..200 ms policies to keep failure paths deterministic.

## Coverage

| Test IDs | Level | Report | Contract |
| --- | --- | --- | --- |
| T05-ROUTE-01..02 | Unit | `server_unit.xml` | Peer section names map to production runtime endpoints. |
| T08-RUNTIME-01..06 | Unit | `server_unit.xml` | Shared bounded pool, stable failure categories, and duration validation. |
| GateVarifyPoolContractTests.* | Unit | `server_unit.xml` | Gate’s Varify stub pool supports borrow/return, bounded acquisition and idempotent close. |
| ChatPeerPoolContractTests.* | Unit | `server_unit.xml` | Chat peer pools obey the same lifecycle contract. |
| T08-GATE-RPC-01..04 | Integration | `server_integration.xml` | Real Gate production clients call the loopback Varify server; deadline, unavailable, and peer-shutdown paths map to `RPCFailed`. |
| T08-CHAT-RPC-01..04 | Integration | `server_chat_grpc_integration.xml` | Real Chat production clients call the loopback Chat peer with the same failure coverage. |
| S01-GRPC-CFG-01 / S02-GRPC-CFG-01 | Integration | `server_integration.xml` | Real Chat/Gate executables reject out-of-range deadline/pool configuration before listening. |

The loopback fixtures bind `127.0.0.1:0`, use scoped server shutdown deadlines, expose no public network, and create no external Redis/MySQL/SMTP dependency. Gate and Chat production executables and their tests link the same `GateGrpcClients` / `ChatGrpcClients` static libraries; there is no copied parser, test-only switch, or private-method test seam.

The Chat success case also verifies that `KickUserReq.session_id` survives the production client and real gRPC transport.

## Execution

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
```

`RunServerTests` app-local deploys and executes all RPC reports, rejects a missing report or exact-count drift, and is part of the required develop CI lane.

## Known gaps

- Cross-instance delivery is exercised by the [two-server acceptance](../../services/README.md).
- The current public error model intentionally collapses internal `PoolExhausted`, `Closed`, `DeadlineExceeded`, `Unavailable`, and `Cancelled` categories to `RPCFailed`. Changing that wire-visible behavior requires a reviewed contract change.
- No automatic retry is implemented or claimed.
