# Process Harness Integration Contracts

This module owns deterministic resources for Phase 3B process and transport integration tests. `RunContext` creates an immutable random run identity, a synthetic identity namespace, a fresh temporary root, exclusive dynamic loopback port reservations, and an absolute deadline. It accepts cleanup only for resources reserved and committed by that context, then executes the ledger in reverse registration order.

The process tests never adopt an existing directory or PID, delete outside the run root, use a fixed shared port, or wait with a fixed sleep. Primary failures and cleanup failures remain separate so teardown cannot hide the reason a scenario failed.

## Stable contracts

- `T09-PROC-01..05`: run identity, ports, temp ownership, deadline and reverse teardown.
- `T09-PROC-06..10`: Win32 start, protocol readiness, identity-scoped stop and evidence.
- `T09-PROC-11..12`: deterministic fault propagation and complete residue release.

All 12 cases belong to the existing `server_integration.xml` report. The focused
JUnit reports emitted 5 RunContext, 5 ProcessHarness, and 2 fault cases before
the public runner baseline was updated.

## Linux ownership and process contracts (3C-01)

`ProcessHarness` selects its platform adapter in one translation unit. Linux
uses a dedicated process group, PID/start-tick identity, nonblocking bounded
stdout/stderr, SIGTERM followed by at most ten seconds before SIGKILL, and
`waitpid` reaping. The leader remains waitable until group cleanup so its PID
cannot be recycled before the last group signal. These contracts assume the
owner does not ignore SIGCHLD or reap the adapter's child elsewhere; they do not
sandbox an adversarial executable that deliberately escapes its process group.

The dependency-free hosted lane runs the real adapter (no copied process code):

```sh
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-01-posix
```

It registers eight CTest cases: source ownership (`T10-BLD-01`) plus abnormal
exit/output, graceful stop, stale identity refusal, escalation, bounded output,
descendant pipe cleanup and invalid executable (`T10-BLD-04..10`). Process cases
are Foundation / Integration with a ten-second outer timeout; source ownership
is Architecture / Static. No Redis/MySQL/SMTP or vcpkg restore is involved.

After production preflight configure/build, the full selector reuses that build
and adds IntegrationHost's six existing component cases as one CTest
(`T10-BLD-02`) and real ProcessHarness/RunContext resource teardown (`T10-BLD-03`):

```sh
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-01 \
  --junit-dir out/phase3c/ownership/junit
```

Both selectors propagate nonzero build/test exits and emit
`linux_build_ownership.xml`. The full selector has ten CTest entries; the
standalone selector has eight. Unused reserved IDs are not empty tests.
Windows retains its twelve existing `T09-PROC` cases in `server_integration.xml`.
POSIX results require an actual Linux runner; configuring MinGW is not Linux proof.
