#include "httpmgr.h"

HttpMgr::HttpMgr() {
    // 连接信号与槽
    connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);
}

void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod)
{
    QByteArray data = QJsonDocument(json).toJson();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));
    auto self = shared_from_this();
    QNetworkReply * reply = _manager.post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [self, reply, req_id, mod]() {
        // 处理错误
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << reply->errorString();
            // 发送信号通知完成
            emit self->sig_http_finish(req_id, mod, "", ErrorCodes::ERR_NETWORK);
            reply->deleteLater();
            return ;
        }

        // 无错误
        QString res = reply->readAll();
        // 发送信号通知完成
        emit self->sig_http_finish(req_id, mod, res, ErrorCodes::SUCCESS);
        reply->deleteLater();
        return ;
    });
}

void HttpMgr::slot_http_finish(ReqId id, Modules mod, QString res, ErrorCodes err)
{
    if (mod == Modules::REGISTERMOD) {
        // 发送信号通知指定模块http的响应结束了
        emit sig_reg_mod_finish(id, res, err);
    }
}

HttpMgr::~HttpMgr() {

}
