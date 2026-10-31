// Runs a scenario without the window:
//
//   gpssim_cli [--file out.bin] [--port 5555] [--seconds N] [--fs Hz] [--if Hz] [--format int8iq|int16iq|int8|int16]
//              [--cn0 dB-Hz] [--lat deg] [--lon deg] [--alt m] [--week N] [--tow s] [--no-iono] [--no-tropo]

#include "sim/IqOutput.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

using namespace sim;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    Scenario s;
    QString path;
    int port = 0;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--no-iono") { s.iono = false; continue; }
        if (args[i] == "--no-tropo") { s.tropo = false; continue; }
        if (i + 1 >= args.size()) break;
        const QString v = args[++i];
        if (args[i - 1] == "--file") path = v;
        else if (args[i - 1] == "--port") port = v.toInt();
        else if (args[i - 1] == "--seconds") s.seconds = v.toDouble();
        else if (args[i - 1] == "--fs") s.fs = v.toDouble();
        else if (args[i - 1] == "--if") s.ifFreq = v.toDouble();
        else if (args[i - 1] == "--cn0") s.cn0 = v.toDouble();
        else if (args[i - 1] == "--lat") s.latDeg = v.toDouble();
        else if (args[i - 1] == "--lon") s.lonDeg = v.toDouble();
        else if (args[i - 1] == "--alt") s.altM = v.toDouble();
        else if (args[i - 1] == "--week") s.week = v.toInt();
        else if (args[i - 1] == "--tow") s.tow = v.toInt();
        else if (args[i - 1] == "--format")
            s.format = v == "int16iq" ? SampleFormat::Int16IQ : v == "int8" ? SampleFormat::Int8Real
                       : v == "int16" ? SampleFormat::Int16Real : SampleFormat::Int8IQ;
    }
    if (path.isEmpty() && port == 0) {
        std::printf("Give --file and/or --port. A good file name is %s\n", qPrintable(suggestedFileName(s)));
        return 1;
    }

    std::atomic<bool> stop{false};
    TeeSink tee;
    FileSink file;
    TcpSink tcp;
    if (!path.isEmpty()) {
        if (!file.open(path)) {
            std::printf("%s\n", qPrintable(file.error()));
            return 2;
        }
        tee.add(&file);
    }
    if (port) {
        if (!isComplex(s.format)) {
            std::printf("Streaming needs an I/Q format\n");
            return 2;
        }
        if (!tcp.listen(quint16(port))) {
            std::printf("%s\n", qPrintable(tcp.error()));
            return 2;
        }
        std::printf("Waiting for a receiver on port %d\n", port);
        std::fflush(stdout);
        tcp.waitForClient(s, stop);
        tee.add(&tcp);
        s.realTime = true;
    }
    Progress last;
    const QString err = runSimulation(s, tee, stop, [&](const Progress& p) {
        last = p;
        std::printf("\r%.1f s  %.1fx real time  %d satellites  ", p.seconds, p.speed, int(p.sats.size()));
        std::fflush(stdout);
    });
    std::printf("\n");
    if (!err.isEmpty()) {
        std::printf("%s\n", qPrintable(err));
        return 3;
    }
    std::printf("truth ECEF %.3f %.3f %.3f, clipped %.6f\n", last.truth.x, last.truth.y, last.truth.z, last.clipped);
    for (const SatStatus& t : last.sats)
        std::printf("PRN %2d az %6.1f el %5.1f doppler %8.1f cn0 %.1f\n", t.prn, t.az, t.el, t.doppler, t.cn0);
    return 0;
}
