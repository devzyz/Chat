# Chat

This repository contains a Qt chat client and the GateServer, StatusServer,
ChatServer and VarifyServer services.

## Current testing plan

Phase 2.5 and Plans 3A-01..05 are complete. The current closeout item is Plan 3A-06 in
[`tests/plans/PHASE-3A-PLAN.md`](tests/plans/PHASE-3A-PLAN.md). Its current required
packages are restored and each plan proceeds only after its authorized read-only DG-25 preflight. Local
vcpkg paths remain immutable by default; builds and tests may not restore, install,
remove, update, or relocate packages implicitly. Proportional execution and
once-per-phase closeout evidence are defined in
[`tests/CI-GOVERNANCE.md` section 2.1](tests/CI-GOVERNANCE.md#21-比例化执行合同).

## Running one or more ChatServer instances

The repository builds one `ChatServer.exe`. Each process is one server instance,
selected by an explicit configuration file:

```powershell
.\build\windows-servers\Release\ChatServer\ChatServer.exe --config .\ChatServer\ChatServer\configs\chat-01.ini
```

Two example configurations are provided in
`ChatServer\ChatServer\configs\chat-01.ini` and `chat-02.ini`. To run both:

```powershell
.\scripts\chatserver-instances.ps1 -Task Start `
  -Executable .\build\windows-servers\Release\ChatServer\ChatServer.exe `
  -ConfigDirectory .\ChatServer\ChatServer\configs

.\scripts\chatserver-instances.ps1 -Task Status
.\scripts\chatserver-instances.ps1 -Task Stop
```

Each instance configuration must have a unique `[SelfServer].Name` and
`[Log].Name`; every TCP/RPC listener port must also be unique across all managed
instances. The management
script also gives every instance a separate working directory, so relative
`LogDir` values do not cause log files to overlap. Peer and StatusServer entries
are maintained manually for now. A temporarily unavailable peer does not block
startup; invalid local configuration or a local port bind failure does.

See `WINDOWS_BUILD.md` for the complete Windows development setup.
