#include "IqOutput.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstring>

namespace sim {

bool FileSink::open(const QString& path)
{
    file_.setFileName(path);
    return file_.open(QIODevice::WriteOnly);
}

bool FileSink::write(const char* data, qint64 size)
{
    return file_.write(data, size) == size;
}

TcpSink::TcpSink() = default;
TcpSink::~TcpSink() = default;

bool TcpSink::listen(quint16 port)
{
    server_ = std::make_unique<QTcpServer>();
    if (!server_->listen(QHostAddress::Any, port)) {
        error_ = QStringLiteral("Cannot listen on port %1: %2").arg(port).arg(server_->errorString());
        return false;
    }
    return true;
}

bool TcpSink::waitForClient(const Scenario& s, const std::atomic<bool>& stop)
{
    stop_ = &stop;
    while (!stop.load()) {
        if (!server_->waitForNewConnection(100)) continue;
        client_ = server_->nextPendingConnection();
        client_->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        char h[kStreamHeaderSize] = {};
        const uint32_t version = 1, format = s.format == SampleFormat::Int8IQ ? 0 : 1;
        const uint32_t week = uint32_t(s.week), tow = uint32_t(s.tow);
        std::memcpy(h, "GPSSIMIQ", 8);
        std::memcpy(h + 8, &version, 4);
        std::memcpy(h + 12, &format, 4);
        std::memcpy(h + 16, &s.fs, 8);
        std::memcpy(h + 24, &s.ifFreq, 8);
        std::memcpy(h + 32, &week, 4);
        std::memcpy(h + 36, &tow, 4);
        return write(h, sizeof h);
    }
    return false;
}

bool TcpSink::write(const char* data, qint64 size)
{
    if (client_->state() != QAbstractSocket::ConnectedState || client_->write(data, size) != size) {
        error_ = QStringLiteral("The receiver disconnected");
        return false;
    }
    // A receiver that cannot keep up slows the simulation down rather than losing samples
    while (client_->bytesToWrite() > 0 && !stop_->load()) {
        client_->waitForBytesWritten(100);
        if (client_->state() != QAbstractSocket::ConnectedState) {
            error_ = QStringLiteral("The receiver disconnected");
            return false;
        }
    }
    return true;
}

bool TeeSink::write(const char* data, qint64 size)
{
    for (IqSink* sink : sinks_) {
        if (!sink->write(data, size)) {
            error_ = sink->error();
            return false;
        }
    }
    return true;
}

} // namespace sim
