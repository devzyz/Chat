# Server Asio lifecycle contract

ChatServer, GateServer, and StatusServer have same-named pool types, so each production implementation is compiled and linked through its own test executable. All three reuse `asio_pool_contract_tests.cpp`; tests never include production `.cpp` files and use shared-owned completion state so a timeout cannot leave callbacks referring to destroyed stack values.

| Test ID | Executable | Level | Contract |
| --- | --- | --- | --- |
| F03-ASIO-01/02 | `server_unit_tests.exe` | Unit | Dispatch completes; first/repeated `stop` and destruction finish within two seconds. |
| T03-GATE-01/02 | `gate_asio_pool_tests.exe` | Unit | Dispatch completes; first/repeated `Stop` and destruction finish within two seconds. |
| T03-STATUS-01/02 | `status_asio_pool_tests.exe` | Unit | Same contract for StatusServer. |

`RunServerTests` builds both projects with manifest restore disabled, reusing the Server job's one restored dependency tree. Reports are `server_gate_asio.xml` and `server_status_asio.xml`; CI uploads `server_*.xml` and treats missing files as errors.

RED was the real compile error that both constructors were private. GREEN is 2/2 per executable after making lifecycle construction public and making Stop atomic, idempotent, and join-safe. Saturation, task exceptions, cancellation races, and stress remain gaps.
