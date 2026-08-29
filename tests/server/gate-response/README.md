# Gate response allowlist tests

Production entry: `gate::HandleJsonRequest` in
`GateServer/GateServer/GateResponse.h`. `LogicSystem` routes all four public
Gate POST handlers through this same Interface. The Module owns JSON parsing,
the stable error envelope, endpoint-specific response allowlists, and the
exception boundary; callers cannot add response fields after shaping.

| Test ID | Runner testcase(s) | Contract |
| --- | --- | --- |
| T06-GATE-01 | `NonLoginResponseTest.*GetVarifyCode` | verification-code success and failure contain only `error` |
| T06-GATE-02 | `NonLoginResponseTest.*UserRegister` | registration success and failure contain only `error` |
| T06-GATE-03 | `NonLoginResponseTest.*ResetPassword` | password-reset success and failure contain only `error` |
| T06-GATE-04 | `GateLoginResponseTest.*` | login success contains exactly `error/uid/token/host/port`; business and RPC failures contain only `error` |
| T06-GATE-05 | `AllEndpointResponseTest.MalformedJsonReturnsTheStableJsonError/*` | malformed JSON bypasses the endpoint Adapter and returns stable `Error_Json` |
| T06-GATE-06 | `AllEndpointResponseTest.ForbiddenRequestFieldsAndValuesAreNeverReflected/*` | request secrets, PII, and unreviewed fields are never reflected |
| T06-GATE-07 | `AllEndpointResponseTest.InternalExceptionReturnsStableErrorWithoutLoggingDetails/*` | dependency exceptions return stable `RPCFailed` without exception text in response or logs |

Domain/Level: Architecture / Component. The 21 runner testcases use the real
JSON parsing and shaping Module with an in-process callback Adapter. They do
not open a socket and are not Integration tests. Real HTTP loopback behavior
remains a later Integration concern.

Runner/report: `RunServerTests` / `server_component.xml`. The callback Adapter
is deterministic and uses no Redis, MySQL, SMTP, public network, or personal
configuration. There is no asynchronous resource to clean up; logging capture
restores the process default logger at the end of each exception case.

DG-03 is authoritative. Any new public response field requires D-04 contract
review before changing this Module or these tests.
