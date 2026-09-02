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

struct AuthOutcome {
    AuthOutcomeKind kind = AuthOutcomeKind::BeginHttp;
    int module = -1;
    int requestId = -1;
    int businessError = 0;
    std::optional<ServerInfo> server;
};

struct AuthAction {
    AuthFlowId flowId = 0;
    std::optional<AuthActionKind> kind;
    AuthError error = AuthError::None;
    bool accepted = false;
    std::optional<ServerInfo> server;
};

class AuthFlowCoordinator
{
public:
    AuthAction Reduce(AuthFlowId flowId, const AuthOutcome &outcome);

private:
    enum class FlowKind { None, Register, Reset, Login };
    enum class Stage { None, AwaitingHttp, AwaitingTcp, AwaitingChatLogin, Chat, Complete };

    static FlowKind FlowFor(int module, int requestId);
    static int OutcomeKey(const AuthOutcome &outcome);

    AuthFlowId _nextFlowId = 1;
    AuthFlowId _currentFlowId = 0;
    FlowKind _flow = FlowKind::None;
    Stage _stage = Stage::None;
    int _requestId = -1;
    std::set<int> _processedOutcomes;
};

#endif // AUTHFLOWCOORDINATOR_H
