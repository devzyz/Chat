# Chat Logic dispatcher contract

`LogicDispatcher::Submit/Stop` is the production Interface shared by ChatServer and these tests. `Submit` accepts a `LogicMessage` and returns `Accepted`, `Full`, or `Closed`; the constructor callback is the production handler-registry seam and reports whether an ID was handled. The Module hides its queue, worker, condition variable, exact-capacity enforcement, wakeup, and drain lifecycle.

| Test ID | Level | Contract |
| --- | --- | --- |
| T08-LOGIC-01 | Unit | Ready requests with synchronous handlers dispatch in ingress FIFO order. |
| T08-LOGIC-02 | Unit | Concurrent producers dispatch every accepted node exactly once. |
| T08-LOGIC-03 | Unit | Exactly `MAX_DEALQUE` pending messages fit; the next is `Full` without damage. |
| T08-LOGIC-04 | Unit | A waiting worker wakes for a message and for stop. |
| T08-LOGIC-05 | Unit | Stop drains accepted messages and completes within two seconds. |
| T08-LOGIC-06 | Unit | Submit after close is `Closed`; repeated Stop is idempotent. |
| T08-LOGIC-07 | Unit | Unknown IDs do not block later valid messages and diagnostics omit bodies. |
| T08-LOGIC-08 | Unit | A throwing handler does not terminate the worker; later messages run and exception/payload text is not logged. |
| T08-LOGIC-09 | Unit | Two sessions may block in SQL concurrently while a third dispatches; a read on the writer's connection starts after the write completion. |
| T08-LOGIC-10 | Unit | Saturated database admission rejects without executing; accepted results and exceptions drain before stop. |
| T08-LOGIC-11 | Unit | A second Redis authorization and subsequent MySQL query complete while Redis and outbound RPC workers are blocked; full RPC admission rejects only the new call and accepted calls drain in order. |
| T08-LOGIC-12 | Unit | With both Redis workers blocked, 56 routing and 8 critical tasks fit; a later SQL guard rejection cannot execute the write. |
| T08-LOGIC-13 | Unit | A Redis guard holds a logical MySQL slot; approved SQL runs on its own pool ahead of later work and drains at stop. |
| T08-LOGIC-14 | Unit | A recording lease adapter verifies Redis pool ownership and that unregister follows renewal for the same owner. |

The ready queue preserves FIFO among eligible requests. A suspended connection keeps its pending requests within the same ingress budget. Authentication and database completions resume on the dispatcher; login additionally waits for binding. Notifications retain no sender ordering slot and drain in FIFO order.

Domain is Architecture and Level is Unit. These tests use in-process synchronization and a recording lease adapter; no socket or live external service is required. Tests synchronize with condition variables, futures, and barriers with a two-second hard limit and explicitly stop every dispatcher. `RunServerTests` writes the cases to `server_unit.xml`; the Windows Server CI lane uploads `server_*.xml`.

RED is the missing production dispatcher implementation. GREEN links the same `LogicDispatcher` static library into `ChatServer` and `ServerUnitTests`. Mutation restores an off-by-one capacity check and must make the focused capacity test fail before the exact implementation is restored. Real `CSession` socket timing remains Phase 3B; business-handler dependency composition and multi-server flows remain Phase 3C/3D.
