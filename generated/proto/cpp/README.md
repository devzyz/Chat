# Generated C++ protobuf sources

Every `.pb.*` file in this directory is generated from `proto/*.proto` by:

```powershell
.\scripts\windows-local.ps1 -Task GenerateProtocols
```

Do not edit generated files. `CheckProtocols` regenerates into an isolated
directory and fails on missing, extra, or byte-drifted generated outputs before
checking descriptor compatibility.
