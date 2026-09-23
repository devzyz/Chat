#ifndef HTTPMGR_H
#define HTTPMGR_H

#include "singleton.h"
#include "gatehttptransport.h"
#include <QString>
#include <QUrl>
#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include "global.h"
#include "authflowcoordinator.h"

// CRTP
/**
 * @brief The HttpMgr class
 * 用于使用http短链接与服务器进行通信
 */
class HttpMgr : public QObject, public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr>
{
    Q_OBJECT
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
    /** @brief 提交指定模块的 JSON HTTP 请求，完成结果通过请求标识和模块信号分发。 */
    void PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod,
                     AuthFlowId flowId);

private:
    /**
     * 为什么这里要添加一个友元？
     * 首先为了保证是单例模式，需要将构造函数设置为私有
     * 那么在Singleton中，构造对应的单例的时候，会调用HttpMgr的构造函数
     * 但是此时构造函数是私有的，无法访问
     * 因此，通过添加友元的方式，实现对私有构造函数的访问
     */
    friend class Singleton<HttpMgr>;
    /** @brief 初始化对象，用于分发 Gate HTTP 操作的完成结果和模块通知。 */
    HttpMgr();
    GateHttpTransport _transport;

private slots:
    /** @brief 按模块将 HTTP 终态分发给注册、登录或重置密码页面。 */
    void slot_http_finish(AuthFlowId flowId, ReqId id, Modules mod,
                          QString res, ErrorCodes err);

signals:
    /** @brief 通知请求 ID、响应体、错误及模块关联的 HTTP 完成结果。 */
    void sig_http_finish(AuthFlowId flowId, ReqId id, Modules mod,
                         QString res, ErrorCodes err);
    /** @brief 通知注册模块的 HTTP 请求结果。 */
    void sig_reg_mod_finish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err);
    /** @brief 通知重置密码模块的 HTTP 请求结果。 */
    void sig_reset_mod_finish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err);
    /** @brief 通知登录模块的 HTTP 请求结果。 */
    void sig_login_mod_finish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMGR_H
