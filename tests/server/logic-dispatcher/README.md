# Chat Logic dispatcher contract

`LogicDispatcher::Submit/Stop` is the production Interface shared by ChatServer and these tests. `Submit` accepts a `LogicMessage` and returns `Accepted`, `Full`, or `Closed`; the constructor callback is the production handler-registry seam and reports whether an ID was handled. The Module hides per-connection queues, shard workers, global capacity enforcement, wakeup, and drain lifecycle.

| Test ID | Level | Contract |
| --- | --- | --- |
| T08-LOGIC-01 | Unit | Requests on one connection dispatch in ingress FIFO order. |
| T08-LOGIC-02 | Unit | Concurrent producers dispatch every accepted node exactly once. |
| T08-LOGIC-03 | Unit | Two shard handlers run concurrently; their queues share exactly `MAX_DEALQUE` pending positions. |
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
| T08-LOGIC-14 | Unit | A recording lease adapter verifies Redis pool ownership and that overlapping renewals publish the latest count before unregistering the same owner. |
| T08-LOGIC-15 | Unit | The owner's next read waits through successive asynchronous steps while another connection on that shard can run. |
| T08-LOGIC-16 | Unit | Notifications from two shards share one FIFO and finish before stop returns. |

Each shard schedules ready connections and resumes callbacks on the same worker. A suspended connection keeps its pending requests within the same ingress budget. Authentication and database completions resume on the dispatcher; login additionally waits for binding. Notifications retain no sender ordering slot and drain in FIFO order.

Domain is Architecture and Level is Unit. These tests use in-process synchronization and a recording lease adapter; no socket or live external service is required. Tests synchronize with condition variables, futures, and barriers with a two-second hard limit and explicitly stop every dispatcher. `RunServerTests` writes the cases to `server_unit.xml`; the Windows Server CI lane uploads `server_*.xml`.
