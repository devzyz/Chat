# TLS gateway integration

`node --test test/tls/tls.test.js` uses real loopback TLS sockets and a disposable
one-day localhost certificate. Windows certificate generation uses the existing
PowerShell 7/.NET runtime; Linux uses OpenSSL. No certificate or private key is committed.

The contract covers all three channel kinds, trusted exchange, unknown issuer,
hostname mismatch, loopback-only upstreams, complete channel configuration,
idempotent shutdown and rollback after a listener bind failure. All sockets,
ports and the test-owned certificate directory are released even after failure.
`fixture.js` provides the echo peer for the Qt transport integration contract.

The runner registers this case in `varify_integration.xml`; current counts belong
to `scripts/windows-local.ps1`. Test ID: V09-TLS-01.
