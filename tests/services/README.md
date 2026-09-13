# Disposable dependency coordinator and adapters (3C-02/04/06)

## Current-N evidence gate (3C-09)

The hosted workflow runs the production build and the `3C-07` superset once,
then downloads that run's service evidence into the downstream job. The default
`scripts/linux-ci.sh --phase 3C --configuration Release` invocation aggregates
those existing results; it does not install dependencies or rerun services.
Local callers must provide `CHAT_JOB_RESULTS`, the same-candidate service outputs
and the compatibility inventory result. Missing inputs fail closed.

`serviceReports.js` remains the single registration owner. It writes
`phase3c-reports.json` with actual case IDs/names/results, owner, Level, enclosing
CTest deadline, relative report paths, candidate SHA and report digests. The gate
checks that manifest against the current registration, parses every JUnit case,
and rejects missing, duplicate, skipped, failed, modified or other-SHA service
evidence. Successful build/POSIX/service job results, process and service teardown,
and the coordinator's credential/mail-body redaction result are all mandatory.
The redaction result covers those generated secrets, not an unrestricted scan of
all files or upstream build logs.

`phase3c-gate-evidence` contains the validated manifest, `junit/*.xml`, `gate.json`,
`teardown.json`, `redaction.json` and the compatibility result. One aggregate JUnit
case records the gate verdict; no placeholder CLOSE test IDs are manufactured.
`currentNPass` can be true while `releaseEligible` remains false and all five
runtime combinations are bootstrap skips. No compatibility or release approval
is implied by a successful current-N gate. Windows checks remain separate and
are required before whole-phase acceptance.

```sh
python3 tests/services/gate_test.py
node --test tests/compatibility/bootstrap.test.js
bash scripts/linux-ci.sh --phase 3C --configuration Release --junit-dir out/phase3c/gate
```

The workflow supplies `CHAT_JOB_RESULTS`; optional root overrides are
`CHAT_PHASE3C_SERVICES_ROOT` and `CHAT_COMPATIBILITY_ROOT`. The `--junit-dir`
argument is the aggregate artifact root (reports are under its `junit/` folder).

## Four production processes (3C-07)

Selector `3C-07` retains the adapter reports and adds `linux_four_process.xml`
with eighteen `T10-4PROC` cases. The same-SHA artifact contains formal
Gate/Status/Chat binaries and `FourProcessDriver`, with the existing dynamic
dependency manifest verifier applied to each relocated bundle. Varify runs
from the same checkout and locked Node dependencies; no service-job native
rebuild or dependency restore is needed.

The driver reuses RunContext/ProcessHarness for identity-checked ownership,
termination and reaping, and ChatFrameCodec for public TCP traffic. The
coordinator leases loopback ports and supplies an owned database and synthetic
credentials. Each child has a 240-second lifetime deadline, graceful shutdown
has ten seconds, and escalation fails the case. The outer coordinator has a
680-second execution window, with CTest and shell deadlines of 690/720 seconds.

| IDs | Contracts |
| --- | --- |
| 01..05 | Fresh migration and Varify/Status/Chat/Gate protocol readiness |
| 06..09 | Mailpit code, registration/login/selection, TCP authentication/private chat, durable message and stable-ID retry |
| 10 | Occupied Gate listener rejects the second formal process |
| 11..12 | SMTP outage failure and refreshed-port recovery |
| 13..15 | Redis outage fails closed, application restart with refreshed mapping, durable retry |
| 16 | Real Redis instance count returns to zero after client disconnect |
| 17 | MySQL table lock blocks Gate read; bounded I/O allows process shutdown |
| 18 | Reverse process stop, exact owned data removal, listener release |

Temporary configuration and mail content are not uploaded. Missing cases and
cleanup failure remain failures. Shutdown failures retain only an allowlisted
process name and category (including a numeric exit code); raw child output and
configuration remain private. Three report/port/stop regressions and three compiled
driver regressions support this suite but do not replace hosted acceptance.
Current evidence remains in the main workspace's `docs/Status.md`.

```sh
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-07
node --test tests/services/coordinator.test.js tests/services/fourProcess.test.js
CHAT_FOUR_DRIVER=/absolute/built/FourProcessDriver node --test tests/services/driver.test.js
```

## Infrastructure and adapter baseline

This Architecture / Integration fixture owns one hosted Ubuntu job's Redis,
MySQL and Mailpit services. It is infrastructure proof, **not** production Redis,
MySQL DAO, SMTP adapter, migration or four-process business-flow coverage.
The optional adapter selectors additionally exercise production Redis and SMTP;
schema, MySQL DAO and full business flows remain separate contracts.
Current verification state is recorded in the main
workspace's `docs/Status.md`.

`ServiceRun.cpp` links the existing `process_harness` target; its `RunContext`
generates the single run identity and owns the Node coordinator child and temp
directory. The launcher is built once in Linux preflight. The services job
downloads only that run's artifact, checks source SHA/content checksum and
restores execute permission. It does not restore Qt or vcpkg a second time.

`services.lock.json` pins official Redis/MySQL and upstream Mailpit images.
Docker Registry v2 manifest digests for the recorded tags were queried directly
on 2026-09-08. Source ownership and API references:

- [Redis official image](https://hub.docker.com/_/redis)
- [MySQL official image bootstrap environment](https://hub.docker.com/_/mysql)
- [Mailpit upstream image](https://mailpit.axllent.org/docs/install/docker/)
- [Mailpit v1.24.1 API schema](https://github.com/axllent/mailpit/blob/v1.24.1/server/ui/api/v1/swagger.json)
- [ioredis v5.10.1 API usage](https://github.com/redis/ioredis/tree/v5.10.1)
- [Nodemailer SMTP timeouts](https://nodemailer.com/smtp)

## Isolation and credentials

Service host ports are Docker-assigned, bound only to `127.0.0.1`. Before use,
the coordinator verifies exact job container IDs, locked images and mapped
ports. Personal endpoints and arbitrary container names are rejected.
After an owned container is restarted, its dynamic host ports are inspected again:
the exact ID, locked image, running state and every loopback binding must pass
before the shared runtime endpoints are updated together. Initial ownership still
requires exact agreement with the workflow-injected ports. SMTP, health probes,
Redis clients, cleanup and endpoint evidence consume the refreshed mappings.
MySQL starts with an empty bootstrap password only in this disposable job;
before creating data, the coordinator changes every bootstrap root account to
a random synthetic password and sets a random Redis password. MySQL SQL input
uses stdin, and `docker exec --env MYSQL_PWD` inherits the value by name from
the child environment. No password is put in argv, workflow constants or logs.
Mailpit deliberately uses unauthenticated SMTP on the isolated loopback mapping.

Each run uses a fresh database, Redis key prefix and `example.invalid` recipient.
MySQL proof combines a native-runner mapped TCP handshake with authenticated SQL
inside the exact MySQL container; it does not claim the C++ production adapter
connected through the mapped port. Schema and application adapter tests remain
future plans. SMTP and Redis library probes run natively, using only the
existing Varify lockfile's libraries, without starting the default mail sender.

Bootstrap first waits for the mapped MySQL protocol handshake, so it cannot
modify credentials in the image's temporary initialization server (which disables
networking). Administrative SQL then uses the container's Unix socket and verifies
`CURRENT_USER()` is `root@localhost`. This keeps the authenticated account stable
while rotating both `localhost` and `%` root passwords; TCP loopback with name
resolution disabled can select the latter before its password has changed.
This follows the [official image's socket administration](https://github.com/docker-library/mysql/blob/master/8.4/docker-entrypoint.sh)
and [MySQL account selection rules](https://dev.mysql.com/doc/refman/8.4/en/connection-access.html).

Health acquisition has a 120-second budget, commands at most five seconds,
connection/SMTP/HTTP calls finite deadlines, and container graceful stop ten
seconds. The launcher has a 360-second execution window plus cleanup reserve;
CTest and the job have outer deadlines. Readiness uses actual protocol results,
not fixed sleeps or process presence. Only bounded readiness polling retries.

Finally deletes the exact Redis key, drops only the owned database and searches
Mailpit by run recipient before deleting those exact message IDs. It verifies
the absence of owned data, disconnects clients and stops verified containers in
reverse order. Cleanup failure stays failed. The workflow's `always()` fallback
stops only exact job container IDs with matching locked image identities when
the coordinator cannot finish; it never relabels missing data cleanup as PASS.
GitHub then removes its own service containers/volumes.

## Contracts and commands

The single CTest `phase3c-disposable-services` (`phase3c-services` label) executes
these real cases in sequence and emits individual entries in `linux_services.xml`:

| IDs | Contract |
| --- | --- |
| T10-SVC-01..02 | Owned image/port identity, bounded health, synthetic authentication |
| T10-SVC-03..05 | Isolated Redis, MySQL database and actual SMTP/API correlation |
| T10-SVC-06..08 | Stopped health endpoint, failed SMTP connection and rejected credentials |
| T10-SVC-09..10 | Real Redis stop, failed cleanup reporting and service recovery |
| T10-SVC-11..12 | Owned teardown and evidence exclusion of generated secrets/mail body |

An early failure stops dependent cases rather than marking them passed. Outer
failure evidence preserves the original coordinator XML separately.
Seventeen dependency-free Node tests cover configuration/lock agreement, health
deadline, real child failure/timeout and evidence failure propagation; they are
not disposable-service proof. The bootstrap regression drives the real
coordinator/command adapter with a two-account command substitute and an isolated
TCP greeting; it proves account-selection sequencing, not real MySQL behavior.
Restart regressions drive the actual coordinator lifecycle with a Docker command
substitute that reallocates ports, including both Mailpit bindings and Redis.
Invalid identities/bindings must fail without publishing any partial update;
a temporary loopback HTTP server verifies refreshed health and cleanup requests.
These regressions do not substitute for the hosted Docker lifecycle proof.
Report regressions reject missing, duplicate or failed selected adapter cases;
successful base infrastructure cases cannot hide an adapter failure.
Bootstrap and restart failure diagnostics in `teardown.json` contain only fixed stage names
and allowlisted error categories, never raw SQL, child output or library errors:

Case failures additionally retain allowlisted `MysqlDeadlineExceeded`, session
availability/output-limit categories and numeric `MysqlError:<code>` diagnostics.
`T10-4PROC-08` sends database selection and the UUID count query as separate
statements: `MysqlSession.execute` uses a private delimiter and accepts one
statement per call. Combining the mysql client's `USE` command with `SELECT`
can consume the response marker, time out and close the session needed by cleanup.

```sh
node --test tests/services/coordinator.test.js
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-02
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-adapters
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-data-adapters
```

The latter requires the workflow-injected container/port environment and
`CHAT_SERVICE_LAUNCHER`; it is not a local Docker/vcpkg installation command.
Job `Phase 3C disposable services` uploads `phase3c-services-evidence` with
`linux_services.xml`, `service-endpoints.json`, `teardown.json` and the shared
process cleanup result, including on failure. No personal credentials, full
Docker inspect output, SMTP bodies or raw SQL errors are uploaded.

## Production adapter selectors

`3C-04` adds native hiredis and Node ioredis production adapters; `3C-06` adds
the production SMTP adapter. `3C-adapters` runs both on the same owned fixture.
All retain the twelve base contracts and fail closed on missing selected cases.
`3C-03` adds schema migration and registration; `3C-05` adds migration and
message persistence. `3C-data-adapters` runs all four adapter suites with the
same owned RunContext. These selectors never connect to the personal exported
database or apply migrations to an unversioned existing database.

| Report | IDs | Count | Owner |
| --- | --- | --- | --- |
| `linux_migration.xml` | T10-MIG-01..12 | 12 | [Schema migration](../server/schema-migration/README.md) |
| `linux_message.xml` | T10-MSG-01..20 | 20 | [Message persistence](../server/message-commit/README.md) |
| `linux_redis.xml` | T10-RDS-01..09 | 9 | [Native Redis](../server/data/README.md) |
| `varify_redis.xml` | V08-REDIS-01..06 | 6 | [Node Redis](../../VarifyServer/test/redis/README.md) |
| `varify_smtp.xml` | V09-SMTP-01..12 | 12 | [SMTP](../../VarifyServer/test/smtp/README.md) |

The native adapter binary travels with the same-run launcher, app-local
`libhiredis.so.1`, source SHA and checksums (`CHAT_REDIS_TEST_BINARY`). CMake
copies the locked shared library by its SONAME and links the adapter with
`$ORIGIN` RPATH. Both jobs use `serviceRuntime.js` to reject missing libraries,
build-tree dependencies and libraries outside the exact bundle/system allowlist,
then run the relocated lifecycle test without `LD_LIBRARY_PATH` or `LD_PRELOAD`.
The service job performs no native rebuild or dependency restore.
The message test binary (`CHAT_MESSAGE_TEST_BINARY`) and its dynamic dependency
closure travel in the `message/` subdirectory of that same checksummed artifact.
`messageRuntime.js` packages dependencies only from the locked installed tree
or the explicit system library directory, then verifies the relocated closure
against `libraries.json`; the service job repeats verification before execution.
The locked static MySQL connector exception is unchanged. The message runner
uses only this app-local library path, and never restores dependencies.
Mailpit uses a database inside its disposable container
to preserve earlier fixture messages across the intentional restart. No host
volume or personal mailbox is used. Individual suites remove only their own
data before the coordinator performs final fixture teardown. Hosted execution
remains required; local lifecycle and loopback tests are not acceptance evidence
for actual Redis/Mailpit integration.
# Phase 3D foundation

`phase3d.fixture.json` fixes the logical users, server identities, seed, journey
and deadlines. `twoServerTopology.js` namespaces physical resources by run ID,
renders reciprocal peer RPC and Status discovery configuration, and rejects
missing clients, shared PID/account, or incorrect observed endpoints.
Configuration strings contain a generated credential and must only be written
to private run-owned files; the topology descriptor contains no credential.

The public foundation entry is:

```bash
bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-00
```

It requires the same-source `CHAT_SERVICE_LAUNCHER`, `CHAT_FOUR_BUNDLE`,
`CHAT_E2E_CLIENT`, and job-owned container IDs/mapped ports used by the existing
coordinator. The `two-server-contract` hosted job supplies these inputs and
uploads `phase3d-contract-evidence`. `clientRuntime.js` packs the Qt binary and
its dependency closure in the checksummed launcher artifact; no GUI platform
plugin is needed by this QCoreApplication entry point.

`fiveProcessCases.js` reuses RunContext/ProcessHarness supervisors, the real
schema migration and dependency coordinator. It runs Varify, Status, Gate,
Chat A/B and two production client processes; account credentials stay in
private run-owned configuration/control channels. It registers and logs in
Alice before Bob, observes real Redis connection counts, checks distinct
Status endpoints, and correlates one cross-instance message by UUID/server ID
and text hash in both production models and MySQL. It stops one client while
the other remains active, then stops all owned processes and removes owned data.

Connection-count waits allow 70 seconds for CServer's 60-second Redis publication
cycle. While polling, snapshots keep both live controllers below their independent
60-second inactivity deadline; a stopped controller is excluded. Counts must still
match exactly, and an unchanged count fails at the bounded deadline.

| Report | IDs | Count | Contract |
| --- | --- | --- | --- |
| `linux_phase3d_contract.xml` | E03-CONTRACT-06..12 | 7 | Fresh schema, five-service ready, two client processes, public registration/discovery, durable cross-instance model correlation, independent exit, reverse cleanup |

`phase3d-reports.json` records actual cases and report hashes. Finalization
requires the exact seven IDs, source SHA, matching report bytes, two distinct
observed clients, application/process/dependency teardown and redaction evidence.
Missing or failed evidence remains a failure, including failures before startup.
These seven cases supplement the five loopback
[client process contracts](../../chat/tests/session-driver/README.md);
they do not replace friendship, bidirectional messaging, recovery/history or
N/N-1 compatibility acceptance. An unqualified/full Phase 3D selector returns
nonzero until the downstream phase gate is implemented.

Support regression (provide the configured same-source client binary):

```bash
CHAT_E2E_CLIENT="$PWD/out/build/linux-x64-release/bin/chat_e2e_client" node --test tests/services/twoServerTopology.test.js tests/services/clientControl.test.js tests/services/phase3dEvidence.test.js tests/services/connectionCount.test.js
```

These seven support cases exercise topology rejection, real Node-to-Qt control,
bounded controller failures, delayed count publication and evidence validation. Synthetic validator inputs
are not E2E results; only the real hosted selector can supply that evidence.
