# Qt resource transfer

Test IDs: Q04-RESOURCE-01..05, mapped by method in `tests/TEST-CONTRACT-MATRIX.md`.

`resource_transfer_tests` is a local-only CMake target. Set `RESOURCE_TEST_HOST` to the freshly built
`ResourceTests.exe`, then run it with the selected Qt kit DLLs available. The test launches a real
loopback HTTP/filesystem server with explicit authentication fixtures.
For headless execution use `-platform minimal -style Fusion`.

The resume case creates a real PNG, cancels after 64 KiB is acknowledged, destroys the manager,
recreates it using the same account cache, resumes, downloads and compares the complete bytes.
The invalid-file case rejects an empty file without contacting a server; generic attachments are supported.
The avatar case publishes a cropped PNG through the real HTTP server, restores it in a second account's
isolated installation directory, recreates the cache and verifies failed publication preserves the old image.
Account cleanup uses ChatPage ownership and cancels replies before UI/model destruction.
AvatarCache belongs to the authenticated account session and is destroyed on account reset.
The page case checks incoming attachment recognition, one transfer manager across resizes,
and destruction with pending work. The model case verifies that attachment updates leave text intact.
