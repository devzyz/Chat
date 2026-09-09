# Production Redis adapter

`redis.js` exports `createRedisAdapter` and has no configuration or connection
side effects on import. The default handler composes an adapter from validated
runtime config. Existing handler-visible behavior is retained: reads/queries
return null on a miss or dependency failure; writes return false on failure.

## Local owning runner

`npm test` includes these Foundation tests in `varify_unit.xml` and
`varify_integration.xml` through the normal Windows runner. No local test connects
to a personal Redis service. All synthetic listeners use ephemeral loopback ports
and close accepted sockets even on failure.

| File | Test ID | Level | Contract |
| --- | --- | --- | --- |
| `redis-unit.test.js` | V08-UNT-01 | Unit | Import and factory are lazy; finite/no-replay options are passed to ioredis. |
| `redis-unit.test.js` | V08-UNT-02 | Unit | One SET with EX writes value and TTL atomically. |
| `redis-unit.test.js` | V08-UNT-03 | Unit | Concurrent first operations share one connection attempt. |
| `redis-unit.test.js` | V08-UNT-04 | Unit | Failed contexts are disconnected, mappings remain compatible, next call gets a new client. |
| `redis-unit.test.js` | V08-UNT-05 | Unit | Repeated close is safe and prevents new writes/connections. |
| `redis-unit.test.js` | V08-UNT-06 | Unit | Invalid deadline/TTL values fail before network activity. |
| `redis-integration.test.js` | V08-LOOP-01 | Integration | Real nonresponding AUTH is bounded, with null reply mapping. |
| `redis-integration.test.js` | V08-LOOP-02 | Integration | Closing an in-flight real connection cancels waiters. |
| `redis-integration.test.js` | V08-LOOP-03 | Integration | Refused ephemeral loopback connection fails without retries. |

The six Unit tests use an injected client; the three loopback tests use locked
ioredis and real sockets, but are not real Redis service proof. Each loopback
test has a three-second deadline. Run a focused module with
`node --test test/redis/redis-unit.test.js` or
`node --test test/redis/redis-integration.test.js` from `VarifyServer`.

## Hosted real Redis runner

`redis-suite.js` exports `runRedisCases(coordinator, record)` for `3C-04` /
`3C-adapters`; it uses the owned service run-id, current mapped endpoint and
synthetic password. Its six Integration cases write `varify_redis.xml`:

| Test ID | Level | Contract |
| --- | --- | --- |
| V08-REDIS-01 | Integration | Real read/write/TTL expiry and invalid-expiry failure leave no persistent orphan. |
| V08-REDIS-02 | Integration | Incorrect credentials preserve null/false failure mapping. |
| V08-REDIS-03 | Integration | CLIENT PAUSE triggers command timeout; failed client is replaced after readiness. |
| V08-REDIS-04 | Integration | Same adapter recovers through the shared byte proxy after a real container restart. |
| V08-REDIS-05 | Integration | Close cancels a pending command and prevents a later write. |
| V08-REDIS-06 | Integration | Only this run's node-prefixed keys are removed and absence is verified. |

The restart fixture is shared with the native Redis suite. It restores the
synthetic password, refreshes Docker mappings and preserves the infrastructure
proof key's remaining TTL. It does not parse or emulate Redis. Readiness probes
do not retry failed business operations or convert a failed assertion to success.
The outer service RunContext provides the process deadline and failure cleanup.
Actual hosted pass/fail is recorded only in `docs/Status.md`.

API references: [locked ioredis v5.10.1](https://github.com/redis/ioredis/tree/v5.10.1)
and [hiredis v1.3.0](https://github.com/redis/hiredis/tree/v1.3.0).
