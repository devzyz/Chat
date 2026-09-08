# Disposable dependency coordinator (3C-02)

This Architecture / Integration fixture owns one hosted Ubuntu job's Redis,
MySQL and Mailpit services. It is infrastructure proof, **not** production Redis,
MySQL DAO, SMTP adapter, migration or four-process business-flow coverage.
Those belong to 3C-03..07. Current verification state is recorded in the main
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
Six dependency-free Node tests cover configuration/lock agreement, health
deadline, real child failure/timeout and evidence failure propagation; they are
not disposable-service proof:

```sh
node --test tests/services/coordinator.test.js
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-02
```

The latter requires the workflow-injected container/port environment and
`CHAT_SERVICE_LAUNCHER`; it is not a local Docker/vcpkg installation command.
Job `Phase 3C disposable services` uploads `phase3c-services-evidence` with
`linux_services.xml`, `service-endpoints.json`, `teardown.json` and the shared
process cleanup result, including on failure. No personal credentials, full
Docker inspect output, SMTP bodies or raw SQL errors are uploaded.
