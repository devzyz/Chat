# Process Harness Integration Contracts

This module owns deterministic resources for Phase 3B process and transport integration tests. `RunContext` creates an immutable random run identity, a synthetic identity namespace, a fresh temporary root, exclusive dynamic loopback port reservations, and an absolute deadline. It accepts cleanup only for resources reserved and committed by that context, then executes the ledger in reverse registration order.

The process tests never adopt an existing directory or PID, delete outside the run root, use a fixed shared port, or wait with a fixed sleep. Primary failures and cleanup failures remain separate so teardown cannot hide the reason a scenario failed.

## Stable contracts

- `T09-PROC-01..05`: run identity, ports, temp ownership, deadline and reverse teardown.
- `T09-PROC-06..10`: Win32 start, protocol readiness, identity-scoped stop and evidence.
- `T09-PROC-11..12`: deterministic fault propagation and complete residue release.

All cases belong to the existing `server_integration.xml` report. Actual runner counts are updated only after the executable emits the cases.
