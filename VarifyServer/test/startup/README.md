# Varify startup lifecycle tests

The production `startServer` interface owns bind validation and the start transition.

| Test ID | Level | Contract |
| --- | --- | --- |
| V07-START-01 | Unit | A bind error rejects and never calls `start`. |
| V07-START-02 | Unit | A zero/invalid bound port rejects and never calls `start`. |
| V07-START-03 | Unit | A successful bind starts once and returns the actual port. |

Tests use a fake server and capture logger; they open no port. RED was `startServer is not a function`; GREEN is 3/3. `node server.js` still starts on `0.0.0.0:50051`, now only after a successful bind. Signal-driven graceful shutdown remains a process Integration gap.
