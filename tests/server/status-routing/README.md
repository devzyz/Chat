# Status routing and token contract

`StatusRouting::Assign/Validate` is the single production Interface shared by `StatusServiceImpl` and these tests. It hides connection-count reads, deterministic server selection, token generation, token persistence/readback, and stable public error mapping. `StatusServiceImpl` only shapes existing protobuf replies.

| Test ID | Level | Contract |
| --- | --- | --- |
| T08-STATUS-01 | Unit | Empty server list fails closed. |
| T08-STATUS-02 | Unit | A single server is selected. |
| T08-STATUS-03 | Unit | Smallest valid non-negative decimal count wins. |
| T08-STATUS-04 | Unit | Equal valid counts use runtime `Name` order. |
| T08-STATUS-05 | Unit | Valid count ranks before unknown. |
| T08-STATUS-06 | Unit | All unknown counts use runtime `Name` order. |
| T08-STATUS-07 | Unit | Negative and malformed counts are unknown. |
| T08-STATUS-08 | Component | Successful persistence returns a complete assignment. |
| T08-STATUS-09 | Component | Store false maps to `RPCFailed` and clears fields. |
| T08-STATUS-10 | Component | Store exception uses the same fail-closed envelope. |
| T08-STATUS-11 | Component | Missing UID maps to `UidInvalid`. |
| T08-STATUS-12 | Component | Token mismatch maps to `TokenInvalid`. |
| T08-STATUS-13 | Component | Matching token succeeds. |
| T08-STATUS-14 | Unit | Concurrent selection is deterministic. |

Domain is Business/Architecture. T08-STATUS-01..07/14 are Unit cases in `server_unit.xml`; T08-STATUS-08..13 are Component cases in `server_component.xml`. Focused execution has a two-second hard bound. Concurrency uses a condition-variable barrier and thread-safe deterministic in-memory Adapters; there is no fixed sleep, real Redis, socket, gRPC transport, or personal endpoint.

The internal `StatusStore` port has a production `RedisMgr` Adapter and scoped in-memory test Adapters. The token-source seam has a production UUID Adapter and deterministic test Adapter. Neither internal seam is exposed through the gRPC caller Interface, and tests observe results only through `Assign/Validate`.

RED covered the missing Interface/empty-list case and ignored store false. GREEN links one `StatusRouting` static library into StatusServer, ServerUnitTests, and ServerComponentTests. Ignoring `PutToken(false)` made T08-STATUS-09 fail and was restored. Real Redis command/disconnect/TTL behavior remains Phase 3C; real Status process/business transport composition remains Phase 3B/3C.
