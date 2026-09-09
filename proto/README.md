# Canonical protocol module

The three editable protobuf authorities are split by owning service:

| Source | Owner and consumers |
| --- | --- |
| `varify.proto` | Varify owns; Gate C++ and Varify Node consume |
| `status.proto` | Status owns; Gate, Status, and Chat C++ consume |
| `chat.proto` | Chat owns; Chat C++ alone consumes |

All files retain `package message` and the migration-preexisting service, RPC,
message, field-number, type, and cardinality contracts. Generated C++ under
`generated/proto/cpp` is reproducible output, not an editable authority.

Phase 3C adds optional `ChatMessage.client_msg_uuid` at previously unused field 7.
The existing `TextChatData.uuid` field 1 remains unchanged. An empty UUID denotes
a legacy record without a client idempotency key. Current text submissions require
lowercase canonical hyphenated UUIDs, without braces; the authenticated session
supplies the sender. Success JSON `uuid_msgId` and peer/history `msg_uuid` preserve
the same UUID to server `message_id` mapping after database commit. Failure JSON
adds `commit_error` (UnauthorizedSender, InvalidUuid, InvalidMembership, Conflict,
StorageUnavailable, DeadlineExceeded) and `client_msg_uuids` for exact batch
correlation, retaining the existing numeric error envelope. Storage/deadline
failures are uncertain and retry with the original UUID. Missing receiver routing
does not revoke committed success. Old history remains readable; this does not
claim network exactly-once delivery or N/N-1 runtime compatibility.

```powershell
.\scripts\windows-local.ps1 -Task GenerateProtocols
.\scripts\windows-local.ps1 -Task CheckProtocols
```

The commands require the repository's pinned vcpkg tools. The checked versions
are protobuf/protoc 6.33.4 (`libprotoc 33.4`) and gRPC/grpc_cpp_plugin 1.76.0.
Node loads `varify.proto` directly with lockfile-pinned `@grpc/grpc-js` 1.14.3,
`@grpc/proto-loader` 0.8.0, and `protobufjs` 7.5.5. CI pins Node 22.

`CheckProtocols` regenerates all twelve C++ files in an isolated temporary
directory, rejects missing, modified, or unexpected generated outputs, and
rejects unregistered canonical or legacy service-local proto authorities. It
then creates a current descriptor set and compares it semantically with the
initial release baseline. It rejects
field-number reuse/change, type or cardinality change, removed messages or
services, renamed/removed RPCs, RPC input/output change, and deletion without
reserving both the old number and name.
