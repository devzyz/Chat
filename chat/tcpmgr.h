#ifndef TCPMGR_H
#define TCPMGR_H
#include "global.h"
#include "singleton.h"
#include <QObject>
#include <functional>
#include <QQueue>
#include "userdata.h"
#include "messagerecord.h"
#include "chattcptransport.h"

/** @brief 管理 TCP 传输与业务回包分发，消息持久化和重试交由 MessageService。 */
class TcpMgr : public QObject, public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    /** @brief 复位底层传输并释放连接资源。 */
    ~TcpMgr();

    /** @brief 按消息类型处理完整包体，并通知相应业务请求结果。 */
    void handleMsg(ReqId id, int len, QByteArray data);

    /** @brief 按主动退出语义关闭连接并暂停待发送消息。 */
    void CloseConnection();
    /** @brief 允许新会话提交发送请求。 */
    void beginSession();
    /** @brief 停止消息服务并重置连接；expectedClose 为 true 时暂停待发送消息。 */
    void resetConnection(bool expectedClose);

signals:
    /** @brief 业务状态更新后通知请求结果，error 为服务端或解析错误码。 */
    void requestCompleted(ReqId id, int error);
    /** @brief 通知 TCP 连接尝试结果，尚不表示聊天登录成功。 */
    void sig_tcp_connect_success(bool bSuccess);
    /** @brief 提交待发送包体，由发送槽检查状态后交给传输层。 */
    void sig_send_data(ReqId reqId, QByteArray data);
    /** @brief 通知聊天登录失败及错误码。 */
    void sig_login_failed(int error);
    /** @brief 聊天登录成功后通知界面进入聊天页。 */
    void sig_login_switch_chat();
    /** @brief 通知用户搜索结果。 */
    void sig_tcp_search_user_finish(std::shared_ptr<SearchInfo>);
    /** @brief 通知收到的好友申请。 */
    void sig_tcp_add_friend_apply(std::shared_ptr<ApplyInfo>);
    /** @brief 通知会话消息变化，供界面更新消息列表。 */
    void sig_update_text_chat_msg(int, int, int, std::vector<std::shared_ptr<ChatDataBase>>&);
    /** @brief 通知当前账号被服务端要求下线。 */
    void sig_notify_offline();
    /** @brief 通知连接结束，expectedClose 区分预期关闭与异常断线。 */
    void sig_connection_close(bool expectedClose);
    /** @brief 通知已加载的一页会话列表。 */
    void sig_tcp_load_chat_finish(QJsonArray);
    /** @brief 通知私聊会话创建成功。 */
    void sig_create_private_chat_finish(std::shared_ptr<ChatInfo>);
    /** @brief 通知历史消息页、后续页标记及分页游标。 */
    void sig_tcp_load_chat_msg_finish(int, std::vector<std::shared_ptr<ChatDataBase>>, bool, qint64);
    /** @brief 通知指定会话的历史消息加载失败。 */
    void sig_tcp_load_chat_msg_failed(int);
    /** @brief 好友申请通过后通知联系人列表更新。 */
    void sig_tcp_add_auth_contact_list(std::shared_ptr<AuthInfo> );
    /** @brief 好友申请通过后通知会话列表更新。 */
    void sig_tcp_add_auth_chat_list(std::shared_ptr<ChatInfo>);
    /** @brief 通知服务端已提交消息及 UUID/ID 对应关系，不代表对方已读。 */
    void sig_text_chat_msg_rsp_finish(int, QVector<MessageAcknowledgement>);
    /** @brief 通知指定会话中发送失败的消息 UUID。 */
    void sig_text_chat_msg_failed(int, const QVector<QString> &);
public slots:
    /** @brief 使用选服结果发起新的 TCP 连接尝试。 */
    void slot_tcp_connect(ServerInfo si);

private slots:
    /** @brief 检查发送及认证状态后发送包体；文本请求未发出时标为待核实。 */
    void slot_send_data(ReqId reqId, QByteArray data);
private:
    friend class Singleton<TcpMgr>;
    /** @brief 连接传输及消息服务信号，注册业务回包处理器。 */
    TcpMgr();
    /** @brief 注册聊天登录、好友、消息与回执的回包处理器。 */
    void initHandlers();

    QMap<ReqId, std::function<void(ReqId id, int len, QByteArray)>> _handlers;
    bool _authenticated = false;
    ChatTcpTransport _transport;
    QString _host;
    quint16 _port = 0;
    quint64 _transportFlowId = 0;
    bool _acceptingSends = false;
    bool _expectedClose = false;
    bool _retainingPending = false;

};

#endif // TCPMGR_H
