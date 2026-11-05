// Runs a scenario without the window:
//
//   gpssim_cli [--file out.bin] [--port 5555] [--seconds N] [--fs Hz] [--if Hz] [--format int8iq|int16iq|int8|int16]
//              [--cn0 dB-Hz] [--lat deg] [--lon deg] [--alt m] [--week N] [--tow s] [--no-iono] [--no-tropo]
//              [--almanac file.alm]      (YUMA; the start is its reference time unless --week or --tow is given)
//              [--usrp] [--usrp-args addr=192.168.10.2] [--uhd-library uhd.dll]
//              [--hackrf] [--hackrf-serial digits] [--amp] [--ppm X] [--hackrf-library hackrf.dll]
//              [--tx-gain dB] [--external-ref]

#include "sim/Almanac.h"
#include "sim/HackRfOutput.h"
#include "sim/IqOutput.h"
#include "sim/UsrpOutput.h"

#include <QCoreApplication>
#include <QStringList>

#include <csignal>
#include <cstdio>
#include <memory>

using namespace sim;

namespace {

std::atomic<bool> stop{false};

void onInterrupt(int)
{
    stop = true;      // finish the chunk, so the radio ends its transmission and is left quiet
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    Scenario s;
    QString path, almanacPath;
    int port = 0;
    bool usrpOn = false, hackrfOn = false, weekGiven = false, towGiven = false;
    UsrpSettings usrpSettings;
    HackRfSettings hackrfSettings;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--no-iono") { s.iono = false; continue; }
        if (args[i] == "--no-tropo") { s.tropo = false; continue; }
        if (args[i] == "--usrp") { usrpOn = true; continue; }
        if (args[i] == "--hackrf") { hackrfOn = true; continue; }
        if (args[i] == "--amp") { hackrfSettings.amp = true; continue; }
        if (args[i] == "--external-ref") { usrpSettings.externalRef = hackrfSettings.externalRef = true; continue; }
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
        else if (args[i - 1] == "--week") { s.week = v.toInt(); weekGiven = true; }
        else if (args[i - 1] == "--tow") { s.tow = v.toInt(); towGiven = true; }
        else if (args[i - 1] == "--almanac") almanacPath = v;
        else if (args[i - 1] == "--usrp-args") { usrpSettings.args = v; usrpOn = true; }
        else if (args[i - 1] == "--uhd-library") usrpSettings.library = v;
        else if (args[i - 1] == "--hackrf-serial") { hackrfSettings.serial = v; hackrfOn = true; }
        else if (args[i - 1] == "--hackrf-library") hackrfSettings.library = v;
        else if (args[i - 1] == "--ppm") hackrfSettings.ppm = v.toDouble();
        else if (args[i - 1] == "--tx-gain") usrpSettings.gainDb = hackrfSettings.gainDb = v.toDouble();
        else if (args[i - 1] == "--format")
            s.format = v == "int16iq" ? SampleFormat::Int16IQ : v == "int8" ? SampleFormat::Int8Real
                       : v == "int16" ? SampleFormat::Int16Real : SampleFormat::Int8IQ;
    }
    if (!almanacPath.isEmpty()) {
        const QString error = readYuma(almanacPath, gpsWeekNow(), &s.almanac);
        if (!error.isEmpty()) {
            std::printf("%s\n", qPrintable(error));
            return 2;
        }
        const AlmanacEntry& ref = s.almanac.front();
        if (!weekGiven) s.week = ref.week;
        if (!towGiven) s.tow = int(ref.toa);
        std::printf("Almanac: PRN");
        for (const AlmanacEntry& a : s.almanac) std::printf(a.health ? " (%d unhealthy)" : " %d", a.prn);
        std::printf(", week %d, reference time %.0f s. Starting week %d, %d s\n", ref.week, ref.toa, s.week, s.tow);
    }
    if (path.isEmpty() && port == 0 && !usrpOn && !hackrfOn) {
        std::printf("Give --file, --port, --usrp and/or --hackrf. A good file name is %s\n", qPrintable(suggestedFileName(s)));
        return 1;
    }
    if (usrpOn && hackrfOn) {
        std::printf("Give --usrp or --hackrf, not both: each sets the pace of the simulation\n");
        return 1;
    }

    std::signal(SIGINT, onInterrupt);
    TeeSink tee;
    FileSink file;
    std::unique_ptr<RadioSink> transmitter;
    TcpSink tcp;
    if (!path.isEmpty()) {
        if (!file.open(path)) {
            std::printf("%s\n", qPrintable(file.error()));
            return 2;
        }
        tee.add(&file);
    }
    if (usrpOn || hackrfOn) {
        const QString invalid = s.validate();
        if (!invalid.isEmpty()) {
            std::printf("%s\n", qPrintable(invalid));
            return 2;
        }
        bool opened = false;
        QString error;
        if (usrpOn) {
            auto usrp = std::make_unique<UsrpSink>();
            opened = usrp->open(usrpSettings, s);
            error = usrp->error();
            transmitter = std::move(usrp);
        } else {
            auto hackrf = std::make_unique<HackRfSink>();
            opened = hackrf->open(hackrfSettings, s);
            error = hackrf->error();
            transmitter = std::move(hackrf);
        }
        if (!opened) {
            std::printf("%s\n", qPrintable(error));
            return 2;
        }
        std::printf("%s\nTransmitting on L1: keep the radio cabled to the receiver through attenuation\n",
                    qPrintable(transmitter->description()));
        tee.add(transmitter.get());
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
        s.realTime = !transmitter;      // a radio sets the pace itself
    }
    Progress last;
    const QString err = runSimulation(s, tee, stop, [&](const Progress& p) {
        last = p;
        std::printf("\r%.1f s  %.1fx real time  %d satellites  ", p.seconds, p.speed, int(p.sats.size()));
        if (transmitter) std::printf("%d underflows  ", transmitter->underflows());
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
    if (transmitter) std::printf("Radio: %d underflows, %d lost packets\n", transmitter->underflows(), transmitter->lostPackets());
    return 0;
}
