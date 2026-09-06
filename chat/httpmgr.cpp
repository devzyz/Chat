#include "httpmgr.h"
#include "logmgr.h"

HttpMgr::HttpMgr() {
    // 连接信号与槽
    connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);
    connect(&_transport, &GateHttpTransport::finished, this,
            [this](const GateHttpResult &result) {
        ErrorCodes error = ErrorCodes::ERR_NETWORK;
        if (result.terminal == GateHttpTerminal::Success) {
            error = ErrorCodes::SUCCESS;
        } else if (result.terminal == GateHttpTerminal::MalformedResponse) {
            error = ErrorCodes::ERR_JSON;
        }
        emit sig_http_finish(static_cast<AuthFlowId>(result.flowId),
                             static_cast<ReqId>(result.requestId),
                             static_cast<Modules>(result.module),
                             QString::fromUtf8(result.body), error);
    });
}

// post请求
void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod,
                          AuthFlowId flowId)
{
    GateHttpRequest request;
    request.url = std::move(url);
    request.body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    request.flowId = static_cast<quint64>(flowId);
    request.requestId = static_cast<int>(req_id);
    request.module = static_cast<int>(mod);
    request.deadlineMs = 5000;
    _transport.post(request);
}

void HttpMgr::slot_http_finish(AuthFlowId flowId, ReqId id, Modules mod,
                               QString res, ErrorCodes err)
{
    if (mod == Modules::REGISTERMOD) {
        // 发送信号通知指定模块http的响应结束了
        emit sig_reg_mod_finish(flowId, id, res, err);
    }

    if (mod == Modules::RESETMOD) {
        // 发送信号通知指定模块http的响应结束了
        emit sig_reset_mod_finish(flowId, id, res, err);
    }

    if (mod == Modules::LOGINMOD) {
        emit sig_login_mod_finish(flowId, id, res, err);
    }
}

HttpMgr::~HttpMgr() {
    _transport.reset();
}
