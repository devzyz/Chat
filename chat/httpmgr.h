#ifndef HTTPMGR_H
#define HTTPMGR_H

#include "singleton.h"
#include <QString>
#include <QUrl>
#include <QObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>

// CRTP
class HttpMgr : public QObject, public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr>
{
public:
    /**
     * 为什么设置为公有？
     * 我们是通过Singleton的静态成员变量管理，httpmgr类的实例
     * 通过std::shared_ptr<HttpMgr> _instance来管理
     * Singleton<HttpMgr>析构的时候，会析构对应的成员变量，则会析构_instance
     * 而它由智能指针管理，则智能指针在析构的时候，会调用httpMgr的析构函数
     * 因此只有当它为公有析构的时候，才能够通过智能指针析构掉
     */
    ~HttpMgr();
private:
    /**
     * 为什么这里要添加一个友元？
     * 首先为了保证是单例模式，需要将构造函数设置为私有
     * 那么在Singleton中，构造对应的单例的时候，会调用HttpMgr的构造函数
     * 但是此时构造函数是私有的，无法访问
     * 因此，通过添加友元的方式，实现对私有构造函数的访问
     */
    friend class Singleton;
    HttpMgr();
    QNetworkAccessManager _manager;
    void PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod);
private slots:
    void slot_http_finish(ReqId id, Modules mod, QString res, ErrorCodes err);

signals:
    void sig_http_finish(ReqId id, Modules mod, QString res, ErrorCodes err);
    void sig_reg_mod_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMGR_H
