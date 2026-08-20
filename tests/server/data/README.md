# Redis pool lifecycle tests

The tests compile the real `RedisConnectionPool` and use a zero-sized pool, so no Redis connection is attempted.

| Test ID | Level | Contract |
| --- | --- | --- |
| T04-RDS-01 | Component | `close()` wakes a blocked finite borrower, which returns null. |
| T04-RDS-02 | Component | Close is idempotent and closed borrows return immediately. |
| T04-RDS-03 | Component | An exhausted borrow returns when its production timeout expires. |

The production borrow interface accepts a finite timeout and observes closed state. The heartbeat worker waits on the same condition variable, so close wakes and joins it promptly. Tests use no `std::future` whose destructor can hide an infinite wait.

Run `RunServerTests` or filter `ServerRedisPoolTests.*`; results are in `server_unit.xml`. RED was the compile failure for the missing timed-borrow overload. GREEN is 3/3 without Redis. Commands, authentication, reconnect/health checks, invalid returned contexts, distributed locks, MySQL, and service-backed cleanup remain Integration gaps.
