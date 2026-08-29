#include "tcpframedecoder.h"

#include <QDataStream>

#include <utility>

QVector<DecodedTcpFrame> TcpFrameDecoder::append(const QByteArray &bytes)
{
    _buffer.append(bytes);
    QVector<DecodedTcpFrame> frames;
    constexpr qsizetype headerSize = sizeof(quint16) * 2;

    while (_buffer.size() >= headerSize) {
        QDataStream stream(_buffer);
        stream.setVersion(QDataStream::Qt_6_0);
        quint16 messageId = 0;
        quint16 bodyLength = 0;
        stream >> messageId;
        stream >> bodyLength;

        if (_buffer.size() < headerSize + bodyLength) {
            break;
        }

        DecodedTcpFrame frame;
        frame.messageId = messageId;
        frame.body = _buffer.mid(headerSize, bodyLength);
        frames.push_back(std::move(frame));
        _buffer.remove(0, headerSize + bodyLength);
    }

    return frames;
}

void TcpFrameDecoder::reset()
{
    _buffer.clear();
}

qsizetype TcpFrameDecoder::bufferedBytes() const
{
    return _buffer.size();
}
