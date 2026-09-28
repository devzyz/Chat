# Linux preflight contract

Git, C++/Qt, JavaScript and PowerShell convention checks and their explicit regression
registration are documented in [conventions/README.md](conventions/README.md).

## CI routing and budgets

`node --test tests/build/ciBudget.test.js` checks develop quick regression, master/weekly/manual full regression,
master-only publication, failure dependencies and the observed cold Windows build budget.
It also checks that server compilation has one owner, registration is checked once before
CI test lanes, the local check bypass is rejected, and only full lanes generate application packages.
The evidence-validator regression runs once in the Linux report job; that job still validates actual reports.
The retired `3C-08` compatibility selector is rejected. Linux keeps its preflight PASS assertion,
but no longer exposes an unused status output. Protocol/schema compatibility tests remain active.
`scripts/ci/test-workflows.ps1 -ToolRoot <temporary-directory>` validates every workflow with pinned actionlint.
`powershell -NoProfile -File scripts/ci/test-vcpkg-github-asset.ps1` checks Release API authentication
using a synthetic `GH_TOKEN`, anonymous access, codeload token isolation, asset identity and SHA512 failure handling.
CI supplies its read-only `github.token` only to steps that download tools or dependency assets.
There is no cross-workflow check poller. The main workflow uses job dependencies and two stable required checks.

`node --test tests/build/ciScope.test.js tests/build/ciBudget.test.js` also checks the conservative
documentation route, obsolete-run cancellation, explicit Linux cold restore and report failure propagation.
Only develop PR/push changes entirely within root README.md, WINDOWS_BUILD.md or docs/**/*.md use static-only
Windows checks. Missing Git objects, unknown paths, renames involving code and mixed changes retain builds.
PR classification uses the complete merge-base range, not the latest commit. Full lanes always build.
No workflow-level path filter or additional Required Check is introduced. Static check failure still blocks merging.
PR metadata edits retain the normal scope so a metadata-only success cannot replace unverified code results.
Develop push runs cancel obsolete develop push runs; master, scheduled and manual runs remain independent.
Service evidence validation runs even after failed upstream/download steps and independently of business validation,
so upstream business failure no longer prevents the service gate from writing its failure report.

## CI binary dependency cache

### Validated weekly Windows toolchain

`node --test tests/build/toolchain.test.js tests/build/vcpkgBinaryCache.test.js tests/build/ciBudget.test.js`
checks tool lock validation, PowerShell patch-version cache isolation, trusted promotion,
default-branch refresh routing, cold-cache selection and early compiler/version guards.
Windows static checks own the toolchain regression; these infrastructure tests do not
change business Test IDs or report counts.

Ordinary PR/push/manual CI selects the newest `windows-toolchain-candidate` artifact from a
completed scheduled or explicit refresh run of `ci.yml` on the repository default branch,
after verifying all four Windows jobs succeeded. A Linux failure does not invalidate that
Windows result. Missing, failed, skipped or duplicate Windows jobs cannot approve a lock.
PRs, other workflows, cancelled/in-progress runs and other branches cannot supply the lock.
Selection happens once per run. API/download/validation failures fail closed. Before the
first promotion, `scripts/ci/windows-toolchain.json` supplies the bootstrap identity.
Candidate records and native snapshots are retained 90 days; an expired approved record requires a new
default-branch refresh rather than silently upgrading. A refresh does not depend on an
old artifact remaining downloadable.

The weekly run (or `gh workflow run ci.yml --ref develop -f refresh_tools=true`) discovers
the latest stable Windows PowerShell/CMake/Ninja releases and the newest MSVC 2022 toolset
and Windows SDK available on the hosted runner. Release SHA256 digests are verified before
recording SHA512 hashes in the candidate vcpkg tool catalog. Windows skips binary
cache restoration for this run. Linux keeps ABI-checked binary cache restoration; use the explicit
manual `cold_linux=true` input to exercise Linux cold restoration independently. Fixed Qt installations
are cached on both platforms; application source is still built and tested. All Windows checks must succeed before approval;
native dependency archives have already been saved on the default branch. A failed Windows
refresh leaves the previous approved record active. Linux remains required for full checks
and release, independently of Windows toolchain approval. The promotion job still publishes
`ci-toolchain-approved` as a human-readable record; selection verifies the candidate's actual
Windows jobs, including legacy runs where Linux failures prevented that publication.

Every Windows build uses the approved exact tool catalog with
`VCPKG_FORCE_DOWNLOADED_BINARIES=1`, fetches each tool, and verifies its executable version
before installing any dependency. MSVC version (and binary digest after the first promotion)
and SDK presence are checked first; the exact toolset/SDK are passed to both vcpkg and
MSBuild. Refresh runs save the complete MSVC toolset plus the versioned SDK Include/Lib/bin
directories in `windows-native-toolchain`. Ordinary runs download that same run's snapshot,
verify its SHA256, and replace only those version directories on the disposable runner before
compiler verification. No local Visual Studio or vcpkg installation is changed. Missing or
corrupt snapshots fail instead of falling back to the runner's compiler. Legacy records without
a snapshot retain the strict preinstalled-compiler check until the next weekly refresh.
The snapshot transport digest is excluded from dependency cache identity: archiving timestamps
must not invalidate otherwise identical compiler/tool inputs. Compiler hashes remain included.

`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ci/test-native-toolchain.ps1`
exercises real archive capture and restoration against temporary directories, including runner
compiler drift, stale-file removal, SDK restoration, digest rejection before writes, and invalid
version paths. Windows static CI runs this regression. It does not install real compilers locally.

Windows v4 cache keys and fallbacks include the complete tool identity; they never restore
a different toolchain namespace. The initial migration can require one cold build, and
cache eviction or dependency changes can still cause legitimate rebuilding. Linux keeps
its existing explicitly supported GCC/Qt/CMake/Ninja/Node versions; GCC drift is checked
before dependency restoration and vcpkg uses its pinned downloaded tools. This maintenance
cycle does not upgrade application dependencies, Qt, Node, GCC or the vcpkg source baseline.
Those changes retain their existing compatibility-review requirements.

Hosted validation must still prove both the first candidate's full regression and a later
ordinary run with zero rebuilt dependencies. Static fixtures do not claim this result.

Run `node --test tests/build/vcpkgBinaryCache.test.js` for the shared Windows/Linux
cache regression. It uses temporary archive fixtures and invokes the public
`scripts/ci/vcpkgBinaryCache.js` CLI; it does not restore or build dependencies.
Windows static checks and Linux preflight execute this test.

The Linux v3 restore order is platform/architecture/image-family/target-and-host-triplet
plus dependency fingerprint, then the same platform/triplet family, then the
explicit v2 keys previously saved by PR #6. `ImageVersion` is diagnostic only;
vcpkg still checks each package ABI, including compiler tracking. The fingerprint
includes the manifest, triplet, optional registry configuration and Linux tool lock.
Only binary archives are cached; current source is always built and tested.

Changed archives and a restored older namespace are saved under a unique run/attempt
key after successful dependency restoration, before business compilation. An unchanged
warm cache is not uploaded. The summary records the restored key, image revision,
actual restored/built package counts, installation time and save reason. Seven-day
artifacts retain the dependency log, installed package ABI records and compiler detection
logs. A failed restore can report diagnostics but cannot save new archives.

Linux runs the manifest install independently, with the same triplets, overlay and
installed root as the production CMake preset. Project configuration stays in preflight
after cache saving, so a CMake project error cannot discard successfully built dependencies.

Regression fixtures cover image updates, dependency edits, platform separation,
v2 migration, immutable refresh keys, unchanged warm inventories and empty-cache
suppression. They do not prove hosted ABI reuse. Validate migration and then a second
unchanged run: complete matching archives should yield zero rebuilt dependencies.
PR caches remain scoped to that PR; develop/master push builds seed their own caches
for later PRs. The first target-branch build may therefore be cold.

## Production preflight

Production source ownership is shared with the explicit MSBuild project entries
through `cmake/ServerSourceOwnership.cmake`. Linux builds the same GateTransport,
GateRequest, GateGrpcClients, StatusTransport, StatusRouting, ChatTransport,
ChatSessionState, LogicDispatcher and ChatGrpcClients libraries used upstream;
formal servers and IntegrationHost consumers link those targets. Generated proto
remains owned by `chat_protocol_cpp`. Conditional or property-expanded source
entries fail configuration instead of silently dropping a source. Run the
central check with `cmake -P tests/build/server_source_ownership.cmake`.

The [process module README](../server/process-harness/README.md) owns 3C-01
commands, registration and the Linux-only evidence boundary.

Plan 3C-00 owns the public selector:

```text
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-00-T2
```

The selector is authoritative only on the pinned GitHub-hosted
`ubuntu-24.04` job. It uses the repository baseline and
`x64-linux-chat-release` triplet with a run-owned `.ci/vcpkg_installed`
directory. It never uses or modifies either DG-25 local Windows dependency
tree.

`linux_preflight_contract.cmake` writes ten stable `T10-LNX-01..10` cases to
the exact `CHAT_JUNIT_PATH` and writes machine-readable status to the exact
`CHAT_EVIDENCE_PATH`. A local/static success is
`READY_FOR_HOSTED_PREFLIGHT`; only the hosted selector may replace that with
`PASS` after compiler, CMake, Qt, vcpkg, canonical proto, compile/link, and
bounded production-main startup checks complete.

`T10-LNX-04` verifies the fixed CMake 3.28.3/Ninja 1.12.1 asset lock and installer ordering.
`scripts/ci/install-linux-tools.sh` uses the shared, digest-checking GitHub Release API downloader,
then checks the extracted executable versions before exposing PATH. The download phase has a
240-second deadline. Only this job's runner-temporary tool directory is used; no local vcpkg tree is modified.
The API path replaces the intermittently failing release-download redirect while retaining tool versions.

`T10-LNX-10-scope-safety` checks workflow password/token values after trimming
whitespace and scalar quotes. Only complete GitHub expressions and the exact
`MYSQL_ALLOW_EMPTY_PASSWORD: "yes"` disposable bootstrap flag are accepted;
literal credentials, other password keys and arbitrary flag values remain
blocked. The same scanner runs positive and negative synthetic regression cases
inside this contract, including multi-space expressions and a bootstrap flag
next to a literal root password. Run the ten static cases without restoring
dependencies:

```sh
cmake -DCHAT_EXPECT=GREEN -DCHAT_JUNIT_PATH=out/phase3c/contract.xml -DCHAT_EVIDENCE_PATH=out/phase3c/contract.json -P tests/build/linux_preflight_contract.cmake
```

When `CHAT_BUILD_CLIENT=ON`, the root CMake project resolves Qt Core 6.5.3 explicitly in its own directory
scope before checking the Qt identity. Qt discovery inside the `chat/` child
directory does not export `Qt6Core_VERSION` to the root. A missing or different
version remains a configuration failure; it is never inferred from the
requested version or replaced by a default.

The Varify loader probe runs the production `server.js` with
`fixtures/varify-loader.json`, loopback endpoints and synthetic non-secret
credential values scoped to its child process. It sends no mail or business
requests and proves only that the production listener starts and remains alive
until the bounded timeout. Redis/SMTP readiness belongs to later integration
plans. Missing configuration, early exit, bind failure or absent startup output
still fails this probe.

The hosted selector runs the same static and mutation functions as this local entry:

```sh
bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-00-contracts
```

This entry performs no restore/build and retains `READY_FOR_HOSTED_PREFLIGHT`, never runtime `PASS`.
It runs ten static cases and fifteen mutations. Each copied fixture must first pass an unmodified
baseline, including the locked-tool JSON and acquisition scripts, before the mutation is applied.
The mutations cover native RPC prerequisites, production targets/package names/source ownership,
vcpkg baseline/host triplet/linkage/checkout depth, missing tool acquisition, invalid asset digest,
CMake version drift and the Varify production-main invocation. Download mutations target the
current installer and asset lock, not the removed third-party acquisition Action.

Any configure, compile, link, loader, startup, identity, timeout, or evidence
failure remains `LINUX_PREFLIGHT_BLOCKED`. The evidence scope is CI
portability; it is not application containerization or Linux release support.

## Server-only Linux configuration

The root CMake project defaults to `CHAT_BUILD_CLIENT=OFF`. It resolves `spdlog`
from the existing vcpkg manifest; it does not enter `chat/` or require Qt.
Use `-DBUILD_TESTING=OFF` for a production-only graph. Hosted identity checks are
opt-in via `CHAT_ENABLE_HOSTED_PREFLIGHT`; the existing `linux-x64-release` preset
explicitly enables client, tests and hosted checks to preserve full CI coverage.

```sh
cmake --preset linux-x64-release -B out/build/linux-server-only \
  -DCHAT_BUILD_CLIENT=OFF -DBUILD_TESTING=OFF -DVCPKG_MANIFEST_INSTALL=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE
cmake --build out/build/linux-server-only --target GateServer StatusServer ChatServer
```

This reuses an existing Linux dependency installation; it does not restore packages.
Outside the pinned hosted runner, also set `-DCHAT_ENABLE_HOSTED_PREFLIGHT=OFF`;
the preset otherwise retains its compiler/CMake/install-root identity checks.
Windows C++ servers continue to use MSBuild. ResourceServer's published platform
remains Windows; this change does not claim Linux ResourceServer runtime support.

`cmake -DCHAT_TEST_NINJA=<ninja-path> -P tests/build/server_only_configuration.cmake`
configures the real root graph with dependency stand-ins and rejects any Qt/GTest
discovery or client/test source leakage. It runs in Windows static CI. It proves
configuration only; full Linux CI also compiles all three targets with real dependencies
and Qt discovery explicitly disabled. No native compile is claimed by the stand-ins.
