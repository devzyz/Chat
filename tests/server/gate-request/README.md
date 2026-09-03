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
