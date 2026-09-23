#ifndef AUTHFLOWCOORDINATOR_H
#define AUTHFLOWCOORDINATOR_H

#include "global.h"

#include <QtGlobal>
#include <optional>
#include <set>

using AuthFlowId = quint64;

enum class AuthOutcomeKind {
    BeginHttp,
    HttpNetworkError,
    HttpMalformedJson,
    HttpBusinessError,
    HttpSuccess,
    TcpConnected,
    TcpConnectFailed,
    ChatLoginFailed,
    ChatLoginSucceeded,
    AbnormalDisconnect
};

enum class AuthActionKind {
    StayAndShowError,
    ConnectChat,
    ShowLogin,
    ShowChat
};

enum class AuthError {
    None,
    Network,
    MalformedResponse,
    Business,
    TcpConnection,
    ChatLogin
};

/** @brief 携带认证步骤结果及其请求身份，供流程协调器判断是否接收。 */
struct AuthOutcome {
    AuthOutcomeKind kind = AuthOutcomeKind::BeginHttp;
    int module = -1;
    int requestId = -1;
    int businessError = 0;
    std::optional<ServerInfo> server;
};

/** @brief 返回认证流程动作；accepted 为 false 表示结果被忽略。 */
struct AuthAction {
    AuthFlowId flowId = 0;
    std::optional<AuthActionKind> kind;
    AuthError error = AuthError::None;
    bool accepted = false;
    std::optional<ServerInfo> server;
};

/** @brief 推进注册、重置和登录状态，过滤重复或过期结果，不执行网络和界面操作。 */
class AuthFlowCoordinator
{
public:
    /** @brief 接收步骤结果并返回后续动作；BeginHttp 创建新流程，其他结果必须匹配当前流程。 */
    AuthAction reduce(AuthFlowId flowId, const AuthOutcome &outcome);

private:
    enum class FlowKind { None, Register, Reset, Login };
    enum class Stage { None, AwaitingHttp, AwaitingTcp, AwaitingChatLogin, Chat, Complete };

    /** @brief 根据模块和请求类型确定认证流程，不支持的组合返回 None。 */
    static FlowKind flowFor(int module, int requestId);
    /** @brief 提取步骤结果类型作为当前流程内的去重键。 */
    static int outcomeKey(const AuthOutcome &outcome);

    AuthFlowId _nextFlowId = 1;
    AuthFlowId _currentFlowId = 0;
    FlowKind _flow = FlowKind::None;
    Stage _stage = Stage::None;
    int _requestId = -1;
    std::set<int> _processedOutcomes;
};

#endif // AUTHFLOWCOORDINATOR_H
