# Varify configuration tests

## Production contract

`config.js` reads non-secret service addresses from JSON. Configuration path
precedence is `--config`, then `CHAT_CONFIG`, then the working-directory
`config.json`.

Credentials are required exclusively through these environment variables:

- `CHAT_VARIFY_EMAIL_USER`
- `CHAT_VARIFY_EMAIL_PASS`
- `CHAT_VARIFY_MYSQL_PASSWORD`
- `CHAT_VARIFY_REDIS_PASSWORD`

Missing variables fail fast without printing credential values. Legacy
credential fields in JSON are rejected so plaintext secrets cannot silently be
reintroduced.

## Isolation and execution

`config.test.js` contains nine `node:test` cases. Each case loads the production
module in an isolated child process with temporary JSON and synthetic
credentials. It does not connect to Redis, MySQL, SMTP, or the public network.

Domain is Foundation and Level is Integration because the contract is observed
through a real child-process module load rather than an in-process fake.

```powershell
Set-Location .\VarifyServer
node.exe --test test/config/config.test.js
```

The repository entry point is `scripts/windows-local.ps1 -Task RunVarifyTests`.
CI writes this suite to `build/test-results/varify_integration.xml`.
