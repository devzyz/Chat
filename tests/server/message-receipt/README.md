# Message receipts

Production: `common/message/MessageReceipts.h`, ChatServer TCP handlers and Chat gRPC hints.
S07-RECEIPT-01 `MessageReceipt.RejectsMalformedAndDuplicateReports` and
S07-RECEIPT-02 `MessageReceipt.RevisionIsAnExactCanonicalInteger` are Business / Unit cases in
ServerUnitTests and `server_unit.xml`; they require no database or network.

Real MySQL atomicity, permission checks, replay and revision pagination are exercised by
`MessageSync.ReceiptsAreAuthorizedMonotonicAndDurable` and
`MessageSync.ReceiptPageCannotSkipAnUpgrade` in the owning
[message-sync Integration runner](../message-sync/README.md). Its two production
ChatServer instances also exercise capability negotiation, cross-server receipt hints,
independent receipt synchronization and unchanged legacy-client behavior.
All fixtures use owned temporary data, processes and ports with bounded deadlines.

Run the quick validation with `scripts/windows-local.ps1 -Task RunServerTests -Configuration Release`.
The real MySQL scenarios use `python -B tests/server/message-sync/integration.py` after
building the existing MessageSyncTests, ResourceTests and Qt probe targets with read-only dependencies.
