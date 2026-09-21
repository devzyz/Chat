# Deferred runtime compatibility

The N/N-1 runtime matrix is deferred and is not a required CI or release gate.
The old `3C-08` bootstrap selector, resolver and its three tests have been removed.
They generated unexecuted combinations rather than running an older client, service
or schema. Historical implementation remains available in Git.

Current protocol descriptor/wire regressions and real schema migration tests remain
active. The compatibility marker in `schema/manifest.json` records the missing
historical baseline; it is not publication eligibility and is retained with its
existing migration contract. No unexecuted combination is reported as passed.

If upgrade support is added, define its supported versions and test actual released
binaries and migrations. Do not restore placeholder skips or a release-inventory
approval gate. Current publication policy is owned by
[CI governance](../CI-GOVERNANCE.md); migration behavior by
[schema migration tests](../server/schema-migration/README.md).
