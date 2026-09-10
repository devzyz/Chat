# Runtime compatibility evidence (3C-08)

The repository has no published Release at phase entry. `bootstrap.js` consumes
the complete, successfully fetched GitHub releases inventory (`gh api --paginate
--slurp`) and records five unexecuted combinations: both client/server directions,
both service RPC directions, and N-1 schema migration to N. IDs are
`T10-COMPAT-01..05`; the reserved 18-ID range is not a fabricated test count.

No published release yields `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`, five JUnit skips,
`releaseEligible: false`, and exit code **2**. Current-N development may proceed;
G-017 and runtime compatibility remain open. Invalid/missing API evidence exits
1. Any published release, including a prerelease, yields `BASELINE_REVIEW_REQUIRED`
and exit 1 until its promotion identity and artifact/schema digests are reviewed.
No source rebuild, Actions cache, or descriptor fixture is used as N-1.

The actual promoted-artifact downloader, safe extraction, production Qt runtime
matrix and historical migration execution remain deferred until a legitimate
baseline exists. This bootstrap path must not be described as supported runtime
compatibility. Static descriptor checks retain their existing separate owner.

```sh
node --test tests/compatibility/bootstrap.test.js
CHAT_RELEASE_INVENTORY=/owned/releases.json bash scripts/linux-ci.sh \
  --phase 3C --configuration Release --selector 3C-08
```

The inventory and outputs must belong to the same workflow invocation. The runner
binds evidence to checkout HEAD and the current schema manifest digest. See
the [current-N aggregate gate](../services/README.md#current-n-evidence-gate-3c-09).
