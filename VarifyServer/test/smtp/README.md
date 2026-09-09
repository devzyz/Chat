# Varify SMTP adapter tests

Production owners are `email.js` (lazy Nodemailer adapter), `smtpConfig.js`
(strict options), `config.js` and the default handler composition in `server.js`.
`SendMail` returns only `{status}`: Delivered, Rejected, Unavailable,
DeadlineExceeded, or InvalidConfig. The public verification RPC maps every
non-Delivered result to the existing Exception value. No provider response,
credentials, recipient, or message body is returned or logged by the adapter.

## Local owning tests

From `VarifyServer`:

```powershell
node --test test/smtp/smtp-unit.test.js
node --test test/smtp/smtp-integration.test.js
npm test
```

- Eight Foundation/Unit tests use an injected transport: configuration, auth,
  result classification, late completion, active close, empty acceptance and throws.
- Six Foundation/Integration tests (`V09-SMTP-06..11`) exercise the real locked
  Nodemailer against ephemeral loopback SMTP fault peers: rejected greeting,
  rejected recipient, disconnect, silent greeting, continuously delayed response,
  and active cancellation. These are protocol fault fixtures, not adapter copies
  or Mailpit success evidence. Every case has a 6-second test timeout, a 250-ms
  send deadline and owned socket/timer/server cleanup; no public SMTP is accessed.
- The owning configuration and handler suites additionally test no-auth config
  in a child process and all structured statuses through the single RPC callback.

The normal reports are `varify_unit.xml` and `varify_integration.xml`. The SMTP
fault cases belong to Integration even though they require no Docker.

## Hosted Mailpit selector

The shared Phase 3C dependency coordinator calls
`runSmtpCases(coordinator, record)` from `mailpit-suite.js`, after its own Mailpit
health and baseline delivery. It owns containers/run-id/dynamic ports and writes
`varify_smtp.xml`; this module does not create another service environment.

| ID | Real behavior |
| --- | --- |
| V09-SMTP-01 | Production adapter no-auth delivery to mapped Mailpit port |
| V09-SMTP-02 | Exactly one message correlated through run recipient and subject |
| V09-SMTP-03 | TLS-to-plaintext failure, no corresponding delivery |
| V09-SMTP-04 | Stopped Mailpit rejects connection, finally restarts |
| V09-SMTP-05 | Delivery recovers using refreshed mapped port |
| V09-SMTP-06..11 | The same six real protocol fault tests described above |
| V09-SMTP-12 | Delete only this invocation's message IDs and preserve baseline |

Mailpit needs `MP_DATABASE=/tmp/phase3c-mailpit.db` inside its disposable container
so stop/start preserves prior fixture mail. The shared coordinator finally deletes
run-recipient mail and removes service resources. The SMTP module also cleans its
own messages in `finally`, preserves primary plus cleanup failures, and records
only safe case names/counts; SMTP payload and API message content remain in memory.
Hosted Mailpit success is not inferred from local fault tests.

## Deadline implementation

Locked Nodemailer 8.0.6 `SMTPTransport.close()` only releases its own resources;
it does not abort active SMTP sends. The adapter uses its supported `getSocket`
hook and owns the underlying `net.Socket`: total deadline/close destroys the
socket (also terminating any TLS wrapper), guards completion once, and releases
the transporter. Per-stage connect/greeting/socket/DNS timeouts are finite too.
No unverified Nodemailer AbortSignal API, Promise-only cancellation, automatic
retry or dependency upgrade is used. SMTP acceptance is not an end-user delivery
or network exactly-once guarantee; a timeout near server acceptance is ambiguous.

API references: [SMTP options](https://nodemailer.com/smtp/),
[socket hook](https://nodemailer.com/smtp/proxies/),
[Mailpit storage](https://mailpit.axllent.org/docs/configuration/email-storage/).
