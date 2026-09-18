# Chat

This repository contains a Qt chat client and the GateServer, StatusServer,
ChatServer and VarifyServer services.

## Current status and development

Current progress, remaining verification and next steps are maintained in
[docs/Status.md](docs/Status.md). The project first completes basic chat functionality,
then focuses on measured performance improvements.

[WINDOWS_BUILD.md](WINDOWS_BUILD.md) owns build and release commands;
[tests/README.md](tests/README.md) owns test entries and
[CI governance](tests/CI-GOVERNANCE.md) owns regression and publication policy.
Local vcpkg dependencies remain read-only unless separately authorized.

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
