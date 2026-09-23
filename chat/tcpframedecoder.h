#ifndef TCPFRAMEDECODER_H
#define TCPFRAMEDECODER_H

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

/** @brief 保存从接收缓冲解析出的消息 ID 和独立消息体。 */
struct DecodedTcpFrame {
    quint16 messageId = 0;
    QByteArray body;
};

/** @brief 增量拆分 TCP 字节流，保存未完成帧并在非法帧头后进入错误状态。 */
class TcpFrameDecoder
{
public:
    /** @brief 返回普通 TCP 消息允许的最大 body 字节数。 */
    static constexpr qsizetype MaxBodyBytes() noexcept { return 2048; }

    /** @brief 追加网络字节并输出完整帧；非法帧头设置错误标记，由 reset 清除。 */
    QVector<DecodedTcpFrame> append(const QByteArray &bytes);
    /** @brief 清空残留帧字节及错误状态，供下一条连接复用。 */
    void reset();
    /** @brief 返回尚未组成完整帧的缓冲字节数。 */
    qsizetype bufferedBytes() const;
    /** @brief 查询解码器是否已遇到不可恢复的帧格式错误。 */
    bool hasError() const;

private:
    QByteArray _buffer;
    bool _error = false;
};

#endif // TCPFRAMEDECODER_H
