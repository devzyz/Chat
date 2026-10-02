---
name: chat-performance
description: Trigger Chat repository server performance tests on GitHub Actions, follow a specific run, and retrieve its measured report. Use for Chat performance baselines or smoke/stress runs; excludes local load tests and Qt UI benchmarks.
---

# Chat performance

Run from the Chat checkout. Read `tests/services/performance/README.md` for metric boundaries and `profiles.json` for workloads.
Use the repository helper `scripts/ci/performance.js`; it owns dispatch identity and artifact validation.

When asked to run performance tests, default to remote develop and baseline. Honor an explicitly selected branch, PR or profile.
If the user requests only status or an existing report, inspect that run without triggering another.

```sh
node scripts/ci/performance.js start --ref develop --profile baseline
node scripts/ci/performance.js start --pr 30 --profile smoke
node scripts/ci/performance.js status --receipt <saved-receipt.json>
node scripts/ci/performance.js download --receipt <saved-receipt.json>
```

The helper saves a receipt before dispatch. Preserve its path and the returned GitHub run URL.
An ambiguous dispatch outcome requires `recover --receipt <file>` and bounded status queries; do not submit again.
If the request cannot be uniquely resolved, report the receipt and unresolved state. A retry after a known failed run needs
a concrete diagnosis and the user's existing authorization; stop after three attempts with the same failure.

Run builds, framework tests and load only in GitHub temporary runners. Keep local vcpkg and personal services untouched.
Use `gh run view <id> --json status,conclusion,jobs` for progress and bounded log excerpts for diagnosis.
Downloaded output must match receipt SHA, request ID and profile. Report failed or missing evidence honestly;
never substitute an older green run or lower the workload to label the requested profile successful.

Deliver the local `测试.md` link, GitHub run URL, measured SHA, profile, result and any material limitations.
The report describes a Linux shared-runner same-host baseline; it does not prove Windows/public-network capacity.
Invoking this skill authorizes its requested workflow dispatch and artifact download, not branch pushes, merges or publication.
