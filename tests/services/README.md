# Disposable dependency coordinator and adapters (3C-02/04/06)

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
Fifteen dependency-free Node tests cover configuration/lock agreement, health
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

```sh
node --test tests/services/coordinator.test.js
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-02
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-adapters
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

| Report | IDs | Count | Owner |
| --- | --- | --- | --- |
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
Mailpit uses a database inside its disposable container
to preserve earlier fixture messages across the intentional restart. No host
volume or personal mailbox is used. Individual suites remove only their own
data before the coordinator performs final fixture teardown. Hosted execution
remains required; local lifecycle and loopback tests are not acceptance evidence
for actual Redis/Mailpit integration.
