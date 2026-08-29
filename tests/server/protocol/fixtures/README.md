# Initial wire compatibility fixtures

`initial-release-descriptor.pb` is the initial compatibility baseline because
no independently auditable prior release descriptor existed. It was generated
from migration-preexisting `ChatServer/ChatServer/message.proto` Git blob
`a8a34ebd17d5378376cf611762e5943a4c1bff45` by:

```powershell
node.exe .\scripts\protocol-compatibility.js create-initial-baseline
```

`varify-request-v1.bin` is encoded by the migration-preexisting
`message.GetVarifyReq` descriptor from the checked-in
`varify-request-v1.textproto`. `varify-request-v1-unknown.bin` appends unknown
varint field 99 with value 7. Reproduce both with:

```powershell
node.exe .\scripts\generate-protocol-fixtures.js
```

The only identity is the reserved `.test` address
`compat-user@example.test`. The fixtures contain no verification code, Token,
password, credential, or real email address.
