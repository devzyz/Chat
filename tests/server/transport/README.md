# Transport/session tests

## Production contract and coverage

The production sources are `ChatServer/ChatServer/ChatFrameCodec.*`, `MsgNode.cpp`, and the header-consumption path in `CSession.cpp`. The stable contract is a four-byte, big-endian header containing a 16-bit message id and 16-bit body length. `MsgNode` and `CSession` both use the same codec; the seam contains no socket or policy behavior.

| Test ID | Level | Path | Contract |
| --- | --- | --- | --- |
| T02-FRM-01 | Unit | normal | Encode/decode preserves id and length in network byte order. |
| T02-FRM-02 | Unit | boundary | Zero and `MAX_LENGTH` body sizes are supported. |
| T02-FRM-03 | Unit | failure | A length above `MAX_LENGTH` is rejected by the codec validator. |

These tests complement, rather than repeat, the `messaging` module's buffer ownership and payload-copy assertions.

## Isolation, lifecycle, and teardown

The codec is deterministic and allocation-local: no process, socket, fixed port, wait, credential, or external service is used. Each test destroys its values at scope exit. The seam is justified because it is the byte-level rule already shared by production send and receive paths.

## Local and CI execution

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=ChatFrameCodecTests.*
```

The existing `servers-release` CI job runs the suite and always uploads `build/test-results/server_unit.xml`; a missing report is an error.

## RED -> GREEN evidence

RED was recorded by temporarily expecting decoded id `0x1235` for encoded id `0x1234`: the unified runner exited 1 and named `ChatFrameCodecTests.HeaderUsesBigEndianMessageIdAndBodyLength` (actual 4660, expected 4661). The assertion was restored and the three tests passed.

## Known gaps

- Full `CSession` loopback coverage for partial headers, partial bodies, sticky packets, cancellation, and peer disconnect remains an Integration gap; constructing a session also reaches singleton/server state.
- `CSession` currently stores decoded unsigned header fields in signed `short` members. A high-bit length can become negative before the existing `> MAX_LENGTH` check. That production defect is not frozen as a passing assertion; this module only verifies the safe codec contract.
- Unknown-message/authentication policy is owned by `LogicSystem` and is not specified by this transport seam.
