#ifndef CHATTCPTRANSPORT_H
#define CHATTCPTRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <memory>

/** @brief 携带 TCP 目标、认证流程 ID 和毫秒级连接及写入期限。 */
struct ChatTcpEndpoint
{
    QString host;
    quint16 port = 0;
    quint64 flowId = 0;
    int connectDeadlineMs = 0;
    int writeDeadlineMs = 0;
};

enum class ChatTcpTerminal
{
    Refused,
    ConnectDeadlineExceeded,
    WriteDeadlineExceeded,
    PeerClosed,
    LocalClosed,
    Reset,
    Superseded,
    ProtocolError
};

/** @brief 携带连接代号和认证流程 ID 的完整 TCP 帧值。 */
struct ChatTcpFrame
{
    quint64 generation = 0;
    quint64 flowId = 0;
    quint16 messageId = 0;
    QByteArray body;
};

/** @brief 携带某一连接代号及流程的唯一终止原因。 */
struct ChatTcpOutcome
{
    quint64 generation = 0;
    quint64 flowId = 0;
    ChatTcpTerminal terminal = ChatTcpTerminal::Refused;
};

/** @brief 在所属 Qt 线程管理 TCP 连接及有界帧收发；用连接代号隔离替换连接的旧回调。 */
class ChatTcpTransport final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ChatTcpTransport)

public:
    /** @brief 返回普通 TCP 消息允许的最大 body 字节数。 */
    static constexpr qsizetype MaxBodyBytes() noexcept { return 2048; }

    /** @brief 初始化对象，用于在所属 Qt 线程管理 TCP 连接及有界帧收发。 */
    explicit ChatTcpTransport(QObject *parent = nullptr);
    /** @brief 销毁独占实现并放弃当前连接，停止计时器和旧连接回调。 */
    ~ChatTcpTransport() override;

    /** @brief 替换旧连接并异步连接指定端点，返回用于隔离后续回调的连接代号。 */
    quint64 connectTo(const ChatTcpEndpoint &endpoint);
    /** @brief 将合法帧加入当前连接写队列；false 表示拒绝，true 仅表示接受发送，不代表送达。 */
    bool send(quint16 messageId, const QByteArray &body);
    /** @brief 主动终止当前连接并产生本地主动关闭终态。 */
    void close();
    /** @brief 清空连接及其缓冲，用新的连接代隔离旧回调。 */
    void reset();

signals:
    /** @brief 通知当前连接已建立，尚不表示业务认证成功。 */
    void connected(quint64 generation, quint64 flowId);
    /** @brief 通知当前连接收到完整帧，关联代号可用于丢弃旧连接数据。 */
    void frameReceived(const ChatTcpFrame &frame);
    /** @brief 通知一次传输操作进入终态，携带原流程关联及终止原因。 */
    void finished(const ChatTcpOutcome &outcome);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // CHATTCPTRANSPORT_H
