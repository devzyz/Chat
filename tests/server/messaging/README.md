# Message buffer tests

This module now tests only `MsgNode` buffer ownership: zero initialization, clear/reuse, embedded NUL copying, maximum payload copying, send-length rejection, and unsigned message-ID preservation. Network-order and receive-length validation moved to the deeper production `ChatFrameCodec` contract and are not repeated here.

Domain is Foundation and Level is Unit. The eight in-memory tests use no socket, thread, or external service. Run `RunServerTests` or filter `MsgNodeTests.*:SendNodeTests.*:RecvNodeTests.*`; results are in `server_unit.xml`.

Full TCP partial/sticky packet, peer disconnect, and session-close behavior remains an Integration gap. No test-only production method is exposed.
