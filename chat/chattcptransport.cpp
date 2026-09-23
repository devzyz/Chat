#include "chattcptransport.h"

#include "tcpframedecoder.h"

#include <QDataStream>
#include <QNetworkProxy>
#include <QPointer>
#include <QQueue>
#include <QTcpSocket>
#include <QTimer>

/** @brief 独占 TCP socket、读写缓冲和期限计时器；由外层 QObject 线程调用并通过连接代隔离回调。 */
struct ChatTcpTransport::Impl
{
    /** @brief 保存借用的拥有者并初始化本次传输运行状态。 */
    explicit Impl(ChatTcpTransport *owner)
        : owner(owner)
    {
        connectDeadline.setSingleShot(true);
        writeDeadline.setSingleShot(true);
        QObject::connect(&connectDeadline, &QTimer::timeout, owner,
            /** @brief 将当前连接的建连超时转为唯一终态。 */
            [this] {
            finish(generation, ChatTcpTerminal::ConnectDeadlineExceeded, true);
        });
        QObject::connect(&writeDeadline, &QTimer::timeout, owner,
            /** @brief 将当前写入超时转为唯一终态。 */
            [this] {
            finish(generation, ChatTcpTerminal::WriteDeadlineExceeded, true);
        });
    }

    /** @brief 取消内部网络操作并释放连接，外层对象不能再收到旧回调。 */
    ~Impl()
    {
        abandon();
    }

    /** @brief 结束被替换连接，增加连接代并启动新 socket 的连接与期限。 */
    quint64 connectTo(const ChatTcpEndpoint &next)
    {
        if (active) {
            finish(generation, ChatTcpTerminal::Superseded, true);
        }

        ++generation;
        endpoint = next;
        decoder.reset();
        writes.clear();
        currentWriteBytes = 0;
        connected = false;
        active = true;

        if (endpoint.host.isEmpty() || endpoint.port == 0 || endpoint.flowId == 0
            || endpoint.connectDeadlineMs <= 0 || endpoint.writeDeadlineMs <= 0) {
            finish(generation, ChatTcpTerminal::Refused, false);
            return generation;
        }

        auto *nextSocket = new QTcpSocket(owner);
        nextSocket->setProxy(QNetworkProxy::NoProxy);
        socket = nextSocket;
        const quint64 currentGeneration = generation;

        QObject::connect(nextSocket, &QTcpSocket::connected, owner,
                         /** @brief 仅接受当前 socket 和连接代的连接成功事件。 */
                         [this, nextSocket, currentGeneration] {
            if (!isCurrent(nextSocket, currentGeneration)) {
                return;
            }
            connected = true;
            connectDeadline.stop();
            emit owner->connected(currentGeneration, endpoint.flowId);
        });
        QObject::connect(nextSocket, &QTcpSocket::readyRead, owner,
                         /** @brief 仅解析当前连接收到的字节，协议错误结束连接。 */
                         [this, nextSocket, currentGeneration] {
            if (!isCurrent(nextSocket, currentGeneration)) {
                return;
            }
            const QVector<DecodedTcpFrame> decoded = decoder.append(nextSocket->readAll());
            if (decoder.hasError()) {
                finish(currentGeneration, ChatTcpTerminal::ProtocolError, true);
                return;
            }
            for (const DecodedTcpFrame &value : decoded) {
                ChatTcpFrame frame;
                frame.generation = currentGeneration;
                frame.flowId = endpoint.flowId;
                frame.messageId = value.messageId;
                frame.body = value.body;
                emit owner->frameReceived(frame);
            }
        });
        QObject::connect(nextSocket, &QTcpSocket::bytesWritten, owner,
                         /** @brief 累计当前帧写入字节，完成后继续下一帧。 */
                         [this, nextSocket, currentGeneration](qint64 count) {
            if (!isCurrent(nextSocket, currentGeneration) || currentWriteBytes <= 0) {
                return;
            }
            currentWriteBytes -= count;
            if (currentWriteBytes > 0) {
                writeDeadline.start(endpoint.writeDeadlineMs);
                return;
            }
            writes.dequeue();
            currentWriteBytes = 0;
            startNextWrite();
        });
        QObject::connect(nextSocket, &QTcpSocket::errorOccurred, owner,
                         /** @brief 仅将当前 socket 错误转换为对应终止原因。 */
                         [this, nextSocket, currentGeneration](QAbstractSocket::SocketError error) {
            if (!isCurrent(nextSocket, currentGeneration)) {
                return;
            }
            const ChatTcpTerminal terminal =
                !connected && error == QAbstractSocket::ConnectionRefusedError
                    ? ChatTcpTerminal::Refused
                    : (connected ? ChatTcpTerminal::PeerClosed : ChatTcpTerminal::Refused);
            finish(currentGeneration, terminal, false);
        });
        QObject::connect(nextSocket, &QTcpSocket::disconnected, owner,
                         /** @brief 仅为当前连接处理对端断开。 */
                         [this, nextSocket, currentGeneration] {
            if (isCurrent(nextSocket, currentGeneration)) {
                finish(currentGeneration, ChatTcpTerminal::PeerClosed, false);
            }
        });

        connectDeadline.start(endpoint.connectDeadlineMs);
        nextSocket->connectToHost(endpoint.host, endpoint.port);
        return generation;
    }

    /** @brief 校验当前连接及帧长度后复制入写队列，拒绝时返回 false。 */
    bool send(quint16 messageId, const QByteArray &body)
    {
        if (!active || !connected || !socket || body.size() > ChatTcpTransport::maxBodyBytes()) {
            return false;
        }

        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << messageId << static_cast<quint16>(body.size());
        bytes.append(body);
        writes.enqueue(std::move(bytes));
        startNextWrite();
        return true;
    }

    /** @brief 将当前 TCP 连接以主动关闭原因结束。 */
    void close()
    {
        if (active) {
            finish(generation, ChatTcpTerminal::LocalClosed, true);
        }
    }

    /** @brief 结束当前操作并清空缓存，使迟到回调不再属于下一次请求。 */
    void reset()
    {
        if (active) {
            finish(generation, ChatTcpTerminal::Reset, true);
        } else {
            ++generation;
            decoder.reset();
            writes.clear();
        }
    }

    /** @brief 判断回调的网络对象和代号是否仍对应当前未结束操作。 */
    bool isCurrent(QTcpSocket *candidate, quint64 candidateGeneration) const
    {
        return active && candidateGeneration == generation && socket == candidate;
    }

    /** @brief 在没有在途写入时提交队首完整帧并启动写入期限。 */
    void startNextWrite()
    {
        if (!active || !connected || !socket || currentWriteBytes > 0) {
            return;
        }
        if (writes.isEmpty()) {
            writeDeadline.stop();
            return;
        }

        currentWriteBytes = socket->write(writes.head());
        if (currentWriteBytes < 0) {
            finish(generation, ChatTcpTerminal::PeerClosed, false);
            return;
        }
        if (currentWriteBytes == 0) {
            finish(generation, ChatTcpTerminal::WriteDeadlineExceeded, true);
            return;
        }
        writeDeadline.start(endpoint.writeDeadlineMs);
    }

    /** @brief 只完成匹配代号的当前操作，停止期限并根据调用参数取消网络 I/O。 */
    void finish(quint64 candidateGeneration, ChatTcpTerminal terminal, bool abort)
    {
        if (!active || candidateGeneration != generation) {
            return;
        }

        const ChatTcpOutcome outcome{generation, endpoint.flowId, terminal};
        active = false;
        connected = false;
        connectDeadline.stop();
        writeDeadline.stop();
        decoder.reset();
        writes.clear();
        currentWriteBytes = 0;

        QPointer<QTcpSocket> completed = socket;
        socket.clear();
        if (completed) {
            QObject::disconnect(completed, nullptr, owner, nullptr);
            if (abort && completed->state() != QAbstractSocket::UnconnectedState) {
                completed->abort();
            }
            completed->deleteLater();
        }
        emit owner->finished(outcome);
    }

    /** @brief 解除网络对象与拥有者的连接，清空状态并安排网络对象销毁。 */
    void abandon()
    {
        active = false;
        connectDeadline.stop();
        writeDeadline.stop();
        decoder.reset();
        writes.clear();
        QTcpSocket *abandoned = socket.data();
        socket.clear();
        if (abandoned) {
            QObject::disconnect(abandoned, nullptr, owner, nullptr);
            abandoned->abort();
            delete abandoned;
        }
    }

    ChatTcpTransport *owner;
    QTimer connectDeadline;
    QTimer writeDeadline;
    QPointer<QTcpSocket> socket;
    TcpFrameDecoder decoder;
    QQueue<QByteArray> writes;
    ChatTcpEndpoint endpoint;
    quint64 generation = 0;
    qint64 currentWriteBytes = 0;
    bool connected = false;
    bool active = false;
};

ChatTcpTransport::ChatTcpTransport(QObject *parent)
    : QObject(parent), _impl(std::make_unique<Impl>(this))
{
}

ChatTcpTransport::~ChatTcpTransport() = default;

quint64 ChatTcpTransport::connectTo(const ChatTcpEndpoint &endpoint)
{
    return _impl->connectTo(endpoint);
}

bool ChatTcpTransport::send(quint16 messageId, const QByteArray &body)
{
    return _impl->send(messageId, body);
}

void ChatTcpTransport::close()
{
    _impl->close();
}

void ChatTcpTransport::reset()
{
    _impl->reset();
}
