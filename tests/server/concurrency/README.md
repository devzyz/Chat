# ChatServer Asio pool tests

`AsioIOServicePool` is exercised through its production constructor, `GetIOService`, `stop`, and destructor. Shared-owned synchronization state prevents a late callback from accessing failed-test stack storage.

| Test ID | Level | Contract |
| --- | --- | --- |
| F03-ASIO-01 | Unit | A posted task completes before scoped destruction. |
| F03-ASIO-02 | Unit | First/repeated stop and destruction complete within two seconds. |

Run `RunServerTests` or filter `AsioIOServicePoolTests.*`; the report is `server_unit.xml`. RED was the real private-constructor compile failure; GREEN is 2/2 after exposing the lifecycle interface and making stop atomic and join-safe. Network cancellation races and stress remain gaps.
