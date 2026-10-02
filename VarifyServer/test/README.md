# VarifyServer tests

The directories follow production contracts. Domain and Level are independent
metadata; reports are split by Level.

| Directory | Domain | Level | Coverage |
| --- | --- | --- | --- |
| `protocol` | Foundation | Unit | Error constants and generated service descriptors. |
| `handler` | Business | Unit | Injected Redis/SMTP/UUID/logger behavior and log redaction. |
| `startup/startup-unit.test.js` | Architecture | Unit | Bind/start transitions through a fake server. |
| `config` | Foundation | Integration | Configuration loading in isolated child processes. |
| `rpc` | Architecture | Integration | Real dynamic-loopback gRPC registration and routing. |
| `startup/startup-integration.test.js` | Architecture | Integration | Direct-process bind failure on an occupied loopback port. |
| `smtp` | Foundation | Unit / Integration | SMTP config/results/deadline and real loopback faults; hosted Mailpit owns delivery evidence. |
| `redis` | Foundation | Unit / Integration | Lazy client, atomic TTL, finite failure/close; hosted Redis owns real data/restart evidence. |

From `VarifyServer`, run `npm run test:unit`, `npm run test:integration`, or
`npm test`. The repository entry point is `RunVarifyTests`. CI writes
`build/test-results/varify_unit.xml` and `varify_integration.xml`; a missing
report is an error. Normal tests use no external Redis or public SMTP; the SMTP
Integration suite opens only its own dynamic loopback fault peers. Hosted
`varify_smtp.xml` and `varify_redis.xml` separately record the disposable adapter selectors.
The normal runner registers 33 Unit and 21 Integration cases.

Successful default-process readiness, full real-dependency composition, signals, cross-language
C++ calls, and C++ deadline behavior remain gaps.

本轮 handler 回归覆盖同邮箱 12 个并发请求只生成/发送一次、冷却复用与消费后重发；
Redis 单元及真实服务合同覆盖 `SET EX NX` 不覆盖已有验证码。默认监听收敛到 loopback。
