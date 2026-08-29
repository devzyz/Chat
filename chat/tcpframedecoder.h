#ifndef TCPFRAMEDECODER_H
#define TCPFRAMEDECODER_H

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

struct DecodedTcpFrame {
    quint16 messageId = 0;
    QByteArray body;
};

class TcpFrameDecoder
{
public:
    QVector<DecodedTcpFrame> append(const QByteArray &bytes);
    void reset();
    qsizetype bufferedBytes() const;

private:
    QByteArray _buffer;
};

#endif // TCPFRAMEDECODER_H
