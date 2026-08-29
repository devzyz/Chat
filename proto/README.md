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
