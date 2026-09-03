#include "authflowcoordinator.h"

AuthFlowCoordinator::FlowKind AuthFlowCoordinator::FlowFor(int module, int requestId)
{
    if (module == static_cast<int>(Modules::REGISTERMOD)
        && (requestId == static_cast<int>(ReqId::ID_GET_VERIFY_CODE)
            || requestId == static_cast<int>(ReqId::ID_REG_USER))) {
        return FlowKind::Register;
    }
    if (module == static_cast<int>(Modules::RESETMOD)
        && (requestId == static_cast<int>(ReqId::ID_GET_VERIFY_CODE)
            || requestId == static_cast<int>(ReqId::ID_RESET_PWD))) {
        return FlowKind::Reset;
    }
    if (module == static_cast<int>(Modules::LOGINMOD)
        && requestId == static_cast<int>(ReqId::ID_LOGIN_UESR)) {
        return FlowKind::Login;
    }
    return FlowKind::None;
}

int AuthFlowCoordinator::OutcomeKey(const AuthOutcome &outcome)
{
    return static_cast<int>(outcome.kind);
}

AuthAction AuthFlowCoordinator::Reduce(AuthFlowId flowId, const AuthOutcome &outcome)
{
    if (outcome.kind == AuthOutcomeKind::BeginHttp) {
        const FlowKind flow = FlowFor(outcome.module, outcome.requestId);
        if (flow == FlowKind::None) {
            return {};
        }

        _currentFlowId = _nextFlowId++;
        _flow = flow;
        _stage = Stage::AwaitingHttp;
        _requestId = outcome.requestId;
        _processedOutcomes.clear();
        return {_currentFlowId, std::nullopt, AuthError::None, true, std::nullopt};
    }

    if (flowId == 0 || flowId != _currentFlowId) {
        return {};
    }

    const bool httpOutcome = outcome.kind == AuthOutcomeKind::HttpNetworkError
        || outcome.kind == AuthOutcomeKind::HttpMalformedJson
        || outcome.kind == AuthOutcomeKind::HttpBusinessError
        || outcome.kind == AuthOutcomeKind::HttpSuccess;
    if (httpOutcome && (FlowFor(outcome.module, outcome.requestId) != _flow
                        || outcome.requestId != _requestId)) {
        return {};
    }

    const int key = OutcomeKey(outcome);
    if (_stage == Stage::AwaitingTcp
        && outcome.kind == AuthOutcomeKind::TcpConnectFailed) {
        if (!_processedOutcomes.insert(key).second) {
            return {};
        }
        return {flowId, AuthActionKind::StayAndShowError,
                AuthError::TcpConnection, true, std::nullopt};
    }
    if (_stage == Stage::AwaitingTcp
        && outcome.kind == AuthOutcomeKind::TcpConnected) {
        if (!_processedOutcomes.insert(key).second) {
            return {};
        }
        _stage = Stage::AwaitingChatLogin;
        return {flowId, std::nullopt, AuthError::None, true, std::nullopt};
    }
    if (_stage == Stage::AwaitingChatLogin
        && outcome.kind == AuthOutcomeKind::ChatLoginFailed) {
        if (!_processedOutcomes.insert(key).second) {
            return {};
        }
        return {flowId, AuthActionKind::StayAndShowError,
                AuthError::ChatLogin, true, std::nullopt};
    }
    if (_stage == Stage::AwaitingChatLogin
        && outcome.kind == AuthOutcomeKind::ChatLoginSucceeded) {
        if (!_processedOutcomes.insert(key).second) {
            return {};
        }
        _stage = Stage::Chat;
        return {flowId, AuthActionKind::ShowChat,
                AuthError::None, true, std::nullopt};
    }
    if (_stage == Stage::Chat
        && outcome.kind == AuthOutcomeKind::AbnormalDisconnect) {
        if (!_processedOutcomes.insert(key).second) {
            return {};
        }
        _stage = Stage::Complete;
        return {flowId, AuthActionKind::ShowLogin,
                AuthError::None, true, std::nullopt};
    }

    if (_stage != Stage::AwaitingHttp) {
        return {};
    }

    if (outcome.kind == AuthOutcomeKind::HttpSuccess) {
        if (!_processedOutcomes.insert(key).second) {
            return {};
        }
        if (_flow != FlowKind::Login) {
            _stage = Stage::Complete;
            return {flowId, std::nullopt, AuthError::None, true, std::nullopt};
        }
        if (!outcome.server.has_value()) {
            return {};
        }
        _stage = Stage::AwaitingTcp;
        return {flowId, AuthActionKind::ConnectChat, AuthError::None,
                true, outcome.server};
    }

    AuthError error = AuthError::None;
    if (outcome.kind == AuthOutcomeKind::HttpNetworkError) {
        error = AuthError::Network;
    } else if (outcome.kind == AuthOutcomeKind::HttpMalformedJson) {
        error = AuthError::MalformedResponse;
    } else if (outcome.kind == AuthOutcomeKind::HttpBusinessError) {
        error = AuthError::Business;
    } else {
        return {};
    }

    if (!_processedOutcomes.insert(key).second) {
        return {};
    }
    return {flowId, AuthActionKind::StayAndShowError, error,
            true, std::nullopt};
}
