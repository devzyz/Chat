# Chat Logic dispatcher contract

`LogicDispatcher::Submit/Stop` is the production Interface shared by ChatServer and these tests. `Submit` accepts a `LogicMessage` and returns `Accepted`, `Full`, or `Closed`; the constructor callback is the production handler-registry seam and reports whether an ID was handled. The Module hides its queue, worker, condition variable, exact-capacity enforcement, wakeup, and drain lifecycle.

| Test ID | Level | Contract |
| --- | --- | --- |
| T08-LOGIC-01 | Unit | Accepted messages dispatch in global FIFO order. |
| T08-LOGIC-02 | Unit | Concurrent producers dispatch every accepted node exactly once. |
| T08-LOGIC-03 | Unit | Exactly `MAX_DEALQUE` pending messages fit; the next is `Full` without damage. |
| T08-LOGIC-04 | Unit | A waiting worker wakes for a message and for stop. |
| T08-LOGIC-05 | Unit | Stop drains accepted messages and completes within two seconds. |
| T08-LOGIC-06 | Unit | Submit after close is `Closed`; repeated Stop is idempotent. |
| T08-LOGIC-07 | Unit | Unknown IDs do not block later valid messages and diagnostics omit bodies. |

Domain is Architecture and Level is Unit. The only dependency is in-process synchronization; no Adapter, socket, Redis, MySQL, or gRPC is used. Tests synchronize with condition variables, futures, and barriers with a two-second hard limit and explicitly stop every dispatcher. `RunServerTests` writes the cases to `server_unit.xml`; the Windows Server CI lane uploads `server_*.xml`.

RED is the missing production dispatcher implementation. GREEN links the same `LogicDispatcher` static library into `ChatServer` and `ServerUnitTests`. Mutation restores an off-by-one capacity check and must make the focused capacity test fail before the exact implementation is restored. Real `CSession` socket timing remains Phase 3B; business-handler dependency composition and multi-server flows remain Phase 3C/3D.
