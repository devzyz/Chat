# Transport frame tests

`ChatFrameCodec` is the production byte-level contract used by both `MsgNode` and `CSession`: a four-byte big-endian header with unsigned 16-bit message ID and body length. `DecodeValidatedHeader` validates length before `CSession` performs a signed cast or allocation; message IDs are intentionally not policy-validated here.

| Test ID | Level | Contract |
| --- | --- | --- |
| T02-FRM-01 | Unit | Network-order decode preserves a high-bit message ID. |
| T02-FRM-02 | Unit | The maximum body length and an unknown ID are preserved. |
| T02-FRM-03 | Unit | A body one byte above the receive limit is rejected. |

Domain is Foundation and Level is Unit. Run `RunServerTests` or filter `ChatFrameCodecTests.*`. CI uploads `build/test-results/server_unit.xml` together with the two Asio lifecycle Unit reports.

RED was a real compile failure: `DecodeValidatedHeader` did not exist. GREEN is 3/3 tests, with `CSession` calling the new production interface. Full socket half/sticky packet, disconnect, and cancellation behavior remains an Integration gap. Unknown IDs remain a `LogicSystem` no-handler concern, not a frame-validation rule.

`history_response_tests` (`E03-RECOVER-CONTRACT-01`) is a separate Business / Unit support regression for the
production `HistoryResponse.h` serializer used by history response 1028. It checks
encoded body size against the real codec, complete prefix preservation, forward
traversal, final/empty pages, oversized legacy rows and membership errors. The Linux
preflight builds this target and runs CTest `phase3d-history-response` (five-second
deadline). It does not change the Windows Unit report count or replace the real
database/client recovery cases `E03-RECOVER-03..04`.
