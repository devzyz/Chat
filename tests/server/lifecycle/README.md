# Server Asio lifecycle contract

ChatServer, GateServer, and StatusServer have same-named pool types, so each production implementation is compiled and linked through its own test executable. All three reuse `asio_pool_contract_tests.cpp`; tests never include production `.cpp` files and use shared-owned completion state so a timeout cannot leave callbacks referring to destroyed stack values.
This directory is the sole documentation and source owner for all six Asio Test IDs; `concurrency` only links here and does not duplicate them.

| Test ID | Executable | Level | Contract |
| --- | --- | --- | --- |
| F03-ASIO-01/02 | `server_unit_tests.exe` | Unit | Dispatch completes; first/repeated `stop` and destruction finish within two seconds. |
| T03-GATE-01/02 | `gate_asio_pool_tests.exe` | Unit | Dispatch completes; first/repeated `Stop` and destruction finish within two seconds. |
| T03-STATUS-01/02 | `status_asio_pool_tests.exe` | Unit | Same contract for StatusServer. |

Domain is Foundation and Level is Unit. `RunServerTests` builds both projects with manifest restore disabled, reusing the Server job's one restored dependency tree. Reports are `server_gate_unit.xml` and `server_status_unit.xml`; CI uploads `server_*.xml` and treats missing files as errors.

RED was the real compile error that both constructors were private. GREEN is 2/2 per executable after making lifecycle construction public and making Stop atomic, idempotent, and join-safe. Saturation, task exceptions, cancellation races, and stress remain gaps.

## Gate MySQL worker shutdown (3C-07 prerequisite)

`gate_mysql_pool_tests` links the production `gate_server_modules` target and
constructs an empty pool, exercising its actual health worker without opening
a database connection. It checks repeated close, rejected borrowing after close,
borrower completion, and destruction with and without explicit close within two
seconds. CTest applies a five-second process timeout and the hosted preflight
runs the `phase3c-gate-mysql-local` label. This is a lifecycle regression, not
real MySQL or four-process acceptance; shutdown during database I/O still needs
the 3C-07 real-dependency fixture.

The Gate lifecycle executable also checks that repeated `LogMgr::Close()` retains
the default logger for later destructor logging. It runs with the same TRACE
compile definition as hosted Linux Release. Previously global spdlog shutdown
cleared that logger before singleton destruction, causing a null dereference.
This regression checks logger availability directly and restores it on failure
so the failure report is not itself lost during static destruction.
