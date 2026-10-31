#pragma once

#include "Simulator.h"

#include <QFile>

#include <memory>

class QTcpServer;
class QTcpSocket;

namespace sim {

// Header sent once on connecting, little-endian. Keep in step with TcpInput.cpp in SoftGpsReceiver.
//   char[8] "GPSSIMIQ", u32 version = 1, u32 format (0 = int8 I/Q, 1 = int16 I/Q), f64 fs, f64 if, u32 week, u32 tow
constexpr int kStreamHeaderSize = 40;

class FileSink : public IqSink
{
public:
    bool open(const QString& path);
    bool write(const char* data, qint64 size) override;
    QString error() const override { return file_.errorString(); }

private:
    QFile file_;
};

// Serves the stream to one receiver, which connects as a client. Used from one thread only.
class TcpSink : public IqSink
{
public:
    TcpSink();
    ~TcpSink() override;
    bool listen(quint16 port);
    // Waits for the receiver. False if stopped first.
    bool waitForClient(const Scenario& s, const std::atomic<bool>& stop);
    bool write(const char* data, qint64 size) override;
    QString error() const override { return error_; }

private:
    std::unique_ptr<QTcpServer> server_;
    QTcpSocket* client_ = nullptr;
    const std::atomic<bool>* stop_ = nullptr;
    QString error_;
};

// Hands the same samples to several sinks
class TeeSink : public IqSink
{
public:
    void add(IqSink* sink) { sinks_.push_back(sink); }
    bool write(const char* data, qint64 size) override;
    QString error() const override { return error_; }

private:
    std::vector<IqSink*> sinks_;
    QString error_;
};

} // namespace sim
