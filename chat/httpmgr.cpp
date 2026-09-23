#include "httpmgr.h"
#include "logmgr.h"

HttpMgr::HttpMgr() {
    // 连接信号与槽
    connect(this, &HttpMgr::httpFinished, this, &HttpMgr::httpFinish);
    connect(&_transport, &GateHttpTransport::finished, this,
            /** @brief 将 HTTP 传输终态转换为旧模块使用的错误码和完成信号。 */
            [this](const GateHttpResult &result) {
        ErrorCodes error = ErrorCodes::ERR_NETWORK;
        if (result.terminal == GateHttpTerminal::Success) {
            error = ErrorCodes::SUCCESS;
        } else if (result.terminal == GateHttpTerminal::MalformedResponse) {
            error = ErrorCodes::ERR_JSON;
        }
        emit httpFinished(static_cast<AuthFlowId>(result.flowId),
                             static_cast<ReqId>(result.requestId),
                             static_cast<Modules>(result.module),
                             QString::fromUtf8(result.body), error);
    });
}

// post请求
void HttpMgr::postHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod,
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

void HttpMgr::httpFinish(AuthFlowId flowId, ReqId id, Modules mod,
                               QString res, ErrorCodes err)
{
    if (mod == Modules::REGISTERMOD) {
        // 发送信号通知指定模块http的响应结束了
        emit registrationHttpFinished(flowId, id, res, err);
    }

    if (mod == Modules::RESETMOD) {
        // 发送信号通知指定模块http的响应结束了
        emit passwordResetHttpFinished(flowId, id, res, err);
    }

    if (mod == Modules::LOGINMOD) {
        emit loginHttpFinished(flowId, id, res, err);
    }
}

HttpMgr::~HttpMgr() {
    _transport.reset();
}
