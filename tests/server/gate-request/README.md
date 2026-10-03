# Gate request orchestration tests

Production entry: `gate::GateRequest::Handle(Endpoint, Json::Value)` in
`GateServer/GateServer/GateRequest.h`. The GateServer executable and Component
tests link the same `GateRequest.vcxproj` production library.

Implemented Component contracts are T08-GATE-01..16. The Module owns the four POST
business sequences, early returns, and stable dependency-error mapping. Its four
internal ports have production Adapters for the existing Gate managers/clients
and scoped in-memory test Adapters that only inject outcomes and record calls.

| Test IDs | Contract |
| --- | --- |
| T08-GATE-01..03 | Missing email, verification success, and verification failure |
| T08-GATE-04..08 | Confirmation mismatch, expired/mismatched code, existing user, and registration success |
| T08-GATE-09..13 | Expired/mismatched code, identity mismatch, update failure, and reset success |
| T08-GATE-14..16 | Credential failure, Status failure, and successful assignment |

Every case verifies ordered calls, zero later calls after an early return, and one
returned `Result`. Dependency false/error/exception paths fail closed through the
existing public `ErrorCodes`. The scoped in-memory Adapters record calls and inject
results or exceptions; they do not reproduce orchestration decisions.

Runner/report: `RunServerTests` / `server_component.xml` (16 of 56 Component cases,
166 Server cases total). Each case is synchronous and bounded by the focused
process's two-second hard limit. Fixtures use synthetic markers and scope cleanup;
no real Redis, MySQL, gRPC, SMTP, socket, credential, or public endpoint is used.

Real Gate HTTP composition remains Phase 3B. Real Redis/MySQL/Varify/Status/SMTP
Adapters remain Phase 3C. Existing T06-GATE response allowlist contracts remain
the sole parser/envelope/secret-field coverage.

本轮补充非法输入、一次性消费失败与用户名/邮箱联合更新参数断言；口令测试验证随机盐、
正确/错误输入、嵌入 NUL、损坏哈希与旧格式判定。真实数据库迁移认证由 GitHub 服务组合验证，
本模块替身不能证明真实 Redis 原子性。报告数量以 runner 注册表为准，上述阶段历史总数不作为门禁。

口令派生属于 Unit，源码 `password_hash_unit_tests.cpp`，由 `ServerUnitTests` 编译并写入 `server_unit.xml`。
