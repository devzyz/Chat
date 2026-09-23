#include "tcpframedecoder.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <iostream>

namespace {

/** 按网络协议组合消息编号、正文长度及正文，供分片与粘包测试。 */
QByteArray frame(quint16 messageId, const QByteArray &body)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << messageId << static_cast<quint16>(body.size());
    bytes.append(body);
    return bytes;
}

/** 断言失败时输出定位信息并返回条件值，供累积测试结果。 */
bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

/** 验证帧头及正文分片、粘包、非法长度和重置后的缓冲状态。 */
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;

    // T07-FRM-01
    TcpFrameDecoder splitHeader;
    const auto firstFrame = frame(0x1234, QByteArray("body", 4));
    passed &= expect(splitHeader.append(firstFrame.left(3)).isEmpty(), "split header emitted a frame early");
    passed &= expect(splitHeader.bufferedBytes() == 3, "split header byte count was not retained");
    const auto completedHeader = splitHeader.append(firstFrame.mid(3));
    passed &= expect(completedHeader.size() == 1, "completed split header did not emit one frame");
    passed &= expect(completedHeader[0].messageId == 0x1234, "message id was not decoded in network order");
    passed &= expect(completedHeader[0].body == QByteArray("body", 4), "split frame body changed");

    // T07-FRM-02
    TcpFrameDecoder splitBody;
    passed &= expect(splitBody.append(firstFrame.left(6)).isEmpty(), "split body emitted a frame early");
    const auto completedBody = splitBody.append(firstFrame.mid(6));
    passed &= expect(completedBody.size() == 1, "completed split body did not emit one frame");
    passed &= expect(completedBody[0].body == QByteArray("body", 4), "completed split body changed");

    // T07-FRM-03
    TcpFrameDecoder adjacent;
    const auto adjacentFrames = adjacent.append(
        frame(1006, QByteArray("one", 3)) + frame(1021, QByteArray()));
    passed &= expect(adjacentFrames.size() == 2, "adjacent frames were not both emitted");
    passed &= expect(adjacentFrames[0].messageId == 1006, "first adjacent frame id changed");
    passed &= expect(adjacentFrames[0].body == QByteArray("one", 3), "first adjacent frame body changed");
    passed &= expect(adjacentFrames[1].messageId == 1021, "second adjacent frame id changed");
    passed &= expect(adjacentFrames[1].body.isEmpty(), "zero-length frame body was not preserved");
    passed &= expect(adjacent.bufferedBytes() == 0, "decoder retained bytes after complete adjacent frames");

    // T07-FRM-04: connection lifecycle reset discards an old partial frame.
    TcpFrameDecoder reconnected;
    const auto staleFrame = frame(1006, QByteArray("stale", 5));
    passed &= expect(reconnected.append(staleFrame.left(6)).isEmpty(),
                     "partial old-connection frame emitted early");
    passed &= expect(reconnected.bufferedBytes() == 6,
                     "partial old-connection frame was not buffered");
    reconnected.reset();
    passed &= expect(reconnected.bufferedBytes() == 0,
                     "connection reset retained old frame bytes");
    const auto freshFrames = reconnected.append(frame(1021, QByteArray("fresh", 5)));
    passed &= expect(freshFrames.size() == 1,
                     "fresh connection frame did not decode independently");
    passed &= expect(freshFrames[0].messageId == 1021,
                     "fresh connection frame inherited the old message id");
    passed &= expect(freshFrames[0].body == QByteArray("fresh", 5),
                     "fresh connection frame inherited old body bytes");

    TcpFrameDecoder history;
    const QByteArray historyBody(65535, 'x');
    const auto historyFrames = history.append(frame(1028, historyBody));
    passed &= expect(historyFrames.size() == 1 && historyFrames[0].body == historyBody,
                     "history response did not preserve the full uint16 body");
    TcpFrameDecoder bounded;
    passed &= expect(bounded.append(frame(1018, QByteArray(2049, 'x'))).isEmpty()
                     && bounded.hasError(), "non-history response exceeded its existing bound");
    return passed ? 0 : 1;
}
