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
    static constexpr qsizetype MaxBodyBytes() noexcept { return 2048; }

    QVector<DecodedTcpFrame> append(const QByteArray &bytes);
    void reset();
    qsizetype bufferedBytes() const;
    bool hasError() const;

private:
    QByteArray _buffer;
    bool _error = false;
};

#endif // TCPFRAMEDECODER_H
