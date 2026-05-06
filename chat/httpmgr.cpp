#include "httpmgr.h"

HttpMgr::HttpMgr() {
    // 连接信号与槽
    connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);
}

// post请求
void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod)
{
    QByteArray data = QJsonDocument(json).toJson();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));
    auto self = shared_from_this();
    // 发起异步post请求，立即返回
    QNetworkReply * reply = _manager.post(request, data);
    // 连接信号与槽，当post异步请求结束后，reply发出QNetworkReply::finished信号，此时执行后面的lambda函数
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

    if (mod == Modules::RESETMOD) {
        // 发送信号通知指定模块http的响应结束了
        emit sig_reset_mod_finish(id, res, err);
    }

    if (mod == Modules::LOGINMOD) {
        emit sig_login_mod_finish(id, res, err);
    }
}

HttpMgr::~HttpMgr() {

}
