# Client auth-flow coordinator tests

## Production Module and Interface

`AuthFlowCoordinator::Reduce(flowId, AuthOutcome) -> AuthAction` is the sole external Interface. The Module owns monotonic flow generation, the current auth stage, stale-result rejection and outcome deduplication. `AuthAction::kind` is optional for a stable no-action result; present kinds remain limited to `StayAndShowError`, `ConnectChat`, `ShowLogin` and `ShowChat`.

HTTP/TCP managers and Login/Register/Reset/MainWindow are production Adapters. Tests submit synthetic outcomes through the same Interface. The reducer performs no I/O, frame parsing, account cleanup or UI construction.

## Contracts

| Test ID | Level | CTest case | Contract |
| --- | --- | --- | --- |
| Q03-AUTH-01 | Unit | `auth_flow.register_network_error` | Register HTTP network failure stays in the flow and reports one stable error action. |
| Q03-AUTH-02 | Unit | `auth_flow.reset_network_error` | Reset HTTP network failure stays in the flow and reports one stable error action. |
| Q03-AUTH-03 | Unit | `auth_flow.login_network_error` | Login HTTP network failure stays in the flow and reports one stable error action. |
| Q03-AUTH-04 | Unit | `auth_flow.unknown_outcome` | Unknown module/request values neither act nor replace the active flow. |
| Q03-AUTH-05 | Unit | `auth_flow.malformed_json` | Malformed JSON stays in the current flow and produces one stable error. |
| Q03-AUTH-06 | Unit | `auth_flow.business_error` | A login business error stays put and cannot produce `ConnectChat`. |
| Q03-AUTH-07 | Unit | `auth_flow.login_http_success` | Login HTTP success produces `ConnectChat` with its payload exactly once. |
| Q03-AUTH-08 | Unit | `auth_flow.tcp_failure` | TCP connection failure reports an error and never creates/shows chat. |
| Q03-AUTH-09 | Unit | `auth_flow.chat_login_failure` | Chat login failure reports an error and cannot produce `ShowChat`. |
| Q03-AUTH-10 | Unit | `auth_flow.chat_login_success` | Chat login success produces `ShowChat` exactly once. |
| Q03-AUTH-11 | Unit | `auth_flow.duplicate_and_late` | Duplicate results and outcomes from an older flow do not act or replace the current flow. |
| Q03-AUTH-12 | Component | `auth_flow.abnormal_disconnect_reset` | Abnormal disconnect produces `ShowLogin`; the production action mapping delegates to existing `ClientSession::resetSession(UnexpectedDisconnect)`. |

Each CTest case has a 10-second hard timeout. Tests use no real network, port, display, credential or fixed sleep; scoped Qt objects clean up at case exit.

## Local runner and remaining gaps

Run `scripts/windows-local.ps1 -Task RunClientTests -Configuration Release`. Unit results belong to `client_unit.xml`; the abnormal-disconnect production wiring case will belong to `client_component.xml`. Real HTTP/TCP transport timing remains Phase 3B and complete login E2E remains Phase 3C/3D.
