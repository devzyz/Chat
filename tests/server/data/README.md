# Redis pool lifecycle tests

The tests compile the real `RedisConnectionPool` and use a zero-sized pool, so no Redis connection is attempted.

| Test ID | Level | Contract |
| --- | --- | --- |
| T04-RDS-01 | Component | `close()` wakes a blocked finite borrower, which returns null. |
| T04-RDS-02 | Component | Close is idempotent and closed borrows return immediately. |
| T04-RDS-03 | Component | An exhausted borrow returns when its production timeout expires. |

The production borrow interface accepts a finite timeout and observes closed state.
All three services now delegate transport lifetime to `common/redis/RedisPool.h`.
It lazily creates bounded hiredis connections and checks idle connections with PING
before reuse. Invalid contexts are discarded. There is no background worker to
detach or join; close wakes pool waiters and frees idle contexts. Callers must
return checked-out connections before destroying the pool.

The original three cases remain Foundation / Component in `server_component.xml`.
Run `RunServerTests` or execute `server_component_tests.exe`.

## Hosted real hiredis integration

`redis_adapter_integration.cpp` compiles the same production pool header, links
hiredis, and runs under the existing service RunContext. `runRedisCases.js` owns
the scenarios; `3C-04` / `3C-adapters` on `scripts/linux-ci.sh` invokes them through
the disposable coordinator. Set the absolute `CHAT_REDIS_TEST_BINARY` to the
built executable. No credentials are passed in argv or included in diagnostics.

| Test ID | Level | Contract |
| --- | --- | --- |
| T10-RDS-01 | Component | Finite borrow, close wakeup, repeated close. |
| T10-RDS-02 | Integration | Binary read/write, atomic SET EX, real TTL expiry. |
| T10-RDS-03 | Integration | Incorrect authentication is rejected safely. |
| T10-RDS-04 | Integration | Real blocking command hits socket timeout; broken connection is discarded. |
| T10-RDS-05 | Integration | CLIENT KILL invalidates an idle connection; checkout reconnects with a new identity. |
| T10-RDS-06 | Integration | A reserved then closed loopback endpoint fails within the deadline. |
| T10-RDS-07 | Integration | Same pool recovers through a stable proxy after an owned Redis container restart. |
| T10-RDS-08 | Integration | Only this run's native keys are removed; cleanup failure is reported. |
| T10-RDS-09 | Integration | A non-responding loopback peer bounds AUTH and safely handles a null reply (not real Redis proof). |

Default connect/command/borrow timeout is 2000 ms; test command timeout is 200 ms.
One borrow makes at most one replacement attempt; no business command is replayed.
Endpoints must be numeric IPv4/IPv6 or `localhost` (mapped to IPv4 loopback).
Other DNS names fail configuration because hiredis synchronous name resolution
does not share its socket deadline. Existing business key names and false-return
dependency mappings are unchanged. Service-specific keys and distributed locks
are not abstracted into the shared transport pool.

The C++ integration runner has a 10-second parent deadline per scenario and a
45-second restart deadline. The proxy only forwards bytes and closes every socket;
it does not emulate Redis. Raw hiredis errors and synthetic credentials are never
printed. No local or hosted real Redis result is implied by merely building this
target: authoritative executed status is recorded in `docs/Status.md`.
