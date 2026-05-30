#ifndef TCPMGR_H
#define TCPMGR_H
#include "global.h"
#include "singleton.h"
#include <QObject>
#include <functional>
#include <QTcpSocket>
#include "userdata.h"

/**
 * @brief The TcpMgr class
 * tcp请求管理单例，tcp网络请求都从这里发出
 */
class TcpMgr : public QObject, public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    ~TcpMgr();

    // 用于对请求回调后的处理，根据不同的ReqId请求类型，构建不同的回调函数
    QMap<ReqId, std::function<void(ReqId id, int len, QByteArray)>> _handlers;
    void initHandlers();

    // 处理从粘包中拆分出单个包体后，应该对包体进行怎样的处理
    void handleMsg(ReqId id, int len, QByteArray data);

    // 连接的tcp服务器的地址和端口号
    QString _host;
    quint16 _port;

    // 与服务器通信的socket
    QTcpSocket _socket;

    // tlv存储结构
    quint16 _message_id;
    quint16 _message_len;
    QByteArray _buffer;

    // 用于标记当前包的包头是否收全了
    bool _b_recv_pending;
signals:
    /**
     * @brief sig_connected_server
     * @param bSuccess
     *
     * 连接完成信号
     */
    void sig_tcp_connect_success(bool bSuccess);
    /**
     * @brief sig_send_data
     *
     */
    void sig_send_data(ReqId reqId, QByteArray data);
    /**
     * @brief sig_login_failed
     * @param error
     * 登录失败信号
     */
    void sig_login_failed(bool error);
    /**
     * @brief sig_login_switch_chat
     * 发送由登录窗口转换为聊天窗口的信号
     */
    void sig_login_switch_chat();
    /**
     * @brief sig_tcp_search_user_finish
     * TCP请求，搜索用户的回包信号
     */
    void sig_tcp_search_user_finish(std::shared_ptr<SearchInfo>);
    /**
     * @brief sig_tcp_add_friend_apply
     * 接收到添加好友申请后，发出信号
     */
    void sig_tcp_add_friend_apply(std::shared_ptr<ApplyInfo>);
    /**
     * @brief sig_tcp_add_auth_friend
     * 添加好友认证，当我点击添加后，我添加对方的逻辑由此信号实现
     */
    void sig_tcp_add_auth_friend(std::shared_ptr<AuthInfo>);
    /**
     * @brief sig_tcp_notify_auth_friend
     * 服务器通知我认证添加好友，当对方同意添加我为好友后，我添加对方的逻辑在此实现
     */
    void sig_tcp_notify_auth_friend(std::shared_ptr<AuthInfo>);
    /**
     * @brief sig_update_text_chat_msg
     * 服务器通知我更新聊天数据，发出信号，通知前端界面更新
     */
    void sig_update_text_chat_msg(int, int, QJsonArray);
public slots:
    /**
     * @brief slot_tcp_connect
     * @param si
     *
     * logindialog内的http回包逻辑，检查到能够正常连接，则发送sig_tcp_connect连接信号
     * 在这里实现tcp连接
     */
    void slot_tcp_connect(ServerInfo si);

private slots:
    /**
     * @brief slot_send_data
     * @param reqId
     * @param data
     *
     * 通过_socket发送数据的槽函数
     */
    void slot_send_data(ReqId reqId, QByteArray data);
private:
    friend class Singleton<TcpMgr>;
    TcpMgr();

signals:

};

#endif // TCPMGR_H
