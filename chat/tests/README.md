# Qt 客户端测试

- [session-driver](session-driver/README.md): GUI/driver shared production login,
  isolated client processes and bounded control-channel lifecycle (five Integration cases).

Qt 测试由 `chat/CMakeLists.txt` 显式注册，测试源码按客户端模块组织：

- [message-model](message-model/README.md)：Business / Unit + Component；前六项保护单一模型规则，store 分页状态和真实 delegate 布局保持 Component。
- [session-reset](session-reset/README.md)：Architecture/Business / Component；保护账号状态、pending batch、页面/模型销毁、幂等和关闭原因。
- [auth-flow](auth-flow/README.md)：Business/Architecture / Unit + Component；保护 HTTP/TCP/Chat login outcome、flow 去重与 abnormal reset production wiring。

从仓库根目录运行：

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
```

The production executable and tests link the same internal
`chat_network_core`, `chat_message_model`, and `chat_auth_flow` modules. Test targets do not
compile drifting copies of those production implementations.

The executable and session reset tests also link the same `chat_session_core`
Module containing `ClientSession`, `TcpMgr`, and `UserMgr`.

- [network-state](network-state/README.md): Foundation / Unit, the `TcpMgr` frame decoder's partial-read,
  adjacent-frame, byte-order, and zero-body state transitions.

CI 的 `client-release` job 按 CTest 标签运行，并生成 `build/test-results/client_unit.xml` 和
`client_component.xml`。新增模块必须创建独立
子目录和 `README.md`，并在 CMake 中显式注册，不能依赖目录通配符发现。

Phase 3C message retry adds two session Component cases and one real loopback
Integration case in the existing session-reset module. The authoritative report
counts are maintained by `scripts/windows-local.ps1`; `client_integration.xml`
includes the authenticated retry alongside the existing HTTP/TCP transport cases.
