#include "tcpframedecoder.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <iostream>

namespace {

QByteArray frame(quint16 messageId, const QByteArray &body)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << messageId << static_cast<quint16>(body.size());
    bytes.append(body);
    return bytes;
}

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

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

    return passed ? 0 : 1;
}
