# Server concurrency test ownership

This directory does not own a separate test source or Test ID. The shared
ChatServer/GateServer/StatusServer `AsioIOServicePool` contracts, their single
source file, Level metadata, runners, reports, timeouts, and known gaps are
owned by the [`lifecycle`](../lifecycle/README.md) Module.

Future concurrency contracts belong here only when they protect a production
Interface distinct from lifecycle. They must receive new Test IDs and explicit
build/report registration rather than duplicating the lifecycle contracts.
