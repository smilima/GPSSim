// Transmits a simulation from a USRP and receives it back on the same USRP, through a cable from TX/RX to RX2 (RF 1 to
// RF 2 on an N200), with attenuation as needed. A USRP can be open in only one process, so this one does both ends:
// what comes back is served over TCP in GPSSim's stream format, for SoftGpsReceiver's "GPSSim simulator (over TCP)"
// source or gnss_cli --tcp, and can be recorded as well.
//
//   usrp_loopback [--usrp-args addr=192.168.10.2] [--tx-gain dB] [--rx-gain dB] [--rx-antenna RX2] [--port 5555]
//                 [--record rx.bin] [--seconds N] [--fs Hz] [--cn0 dB-Hz] [--uhd-library uhd.dll]
//
// --port 0 only records. --seconds 0 (the default) runs until Ctrl+C. The port is GPSSim's, which this stands in
// for, so the receiver needs no change. The rate defaults to 2.5 MS/s: at 5 MS/s the simulation and a receiver on the
// same computer can fall behind the USRP.

#include "sim/IqOutput.h"
#include "sim/UsrpOutput.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <vector>

using namespace sim;

namespace {

// The receiving part of the UHD C API, as in SoftGpsReceiver's UhdInput.cpp
using uhd_error = int;
enum { kStreamStart = 97, kStreamStop = 111, kPolicyAuto = 65, kMdNone = 0x0, kMdTimeout = 0x1, kMdOverflow = 0x8 };
struct StreamArgs { char* cpu_format; char* otw_format; char* args; size_t* channel_list; int n_channels; };
struct StreamCmd { int stream_mode; size_t num_samps; bool stream_now; int64_t time_spec_full_secs; double time_spec_frac_secs; };
struct TuneRequest { double target_freq; int rf_freq_policy; double rf_freq; int dsp_freq_policy; double dsp_freq; char* args; };
struct TuneResult { double clipped_rf_freq, target_rf_freq, actual_rf_freq, target_dsp_freq, actual_dsp_freq; };

std::atomic<bool> stop{false};

void onInterrupt(int)
{
    stop = true;
}

// The receive half. It opens the device a second time, which in the same process gives back the one the transmit
// half has open.
class Receiver
{
public:
    ~Receiver()
    {
        if (md_) rx_metadata_free(&md_);
        if (streamer_) rx_streamer_free(&streamer_);
        if (usrp_) usrp_free(&usrp_);
    }

    bool open(const QString& library, const QString& args, double rate, double gainDb, const QString& antenna, QString* error)
    {
        lib_.setFileName(library);
        if (!lib_.load()) {
            *error = lib_.errorString();
            return false;
        }
        QStringList missing;
        const auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(lib_.resolve(name));
            if (!fn) missing << QLatin1String(name);
        };
        get(usrp_make, "uhd_usrp_make");
        get(usrp_free, "uhd_usrp_free");
        get(usrp_last_error, "uhd_usrp_last_error");
        get(rx_streamer_make, "uhd_rx_streamer_make");
        get(rx_streamer_free, "uhd_rx_streamer_free");
        get(rx_metadata_make, "uhd_rx_metadata_make");
        get(rx_metadata_free, "uhd_rx_metadata_free");
        get(md_error_code, "uhd_rx_metadata_error_code");
        get(get_rx_stream, "uhd_usrp_get_rx_stream");
        get(max_num_samps, "uhd_rx_streamer_max_num_samps");
        get(rx_recv, "uhd_rx_streamer_recv");
        get(issue_stream_cmd, "uhd_rx_streamer_issue_stream_cmd");
        get(set_rx_rate, "uhd_usrp_set_rx_rate");
        get(get_rx_rate, "uhd_usrp_get_rx_rate");
        get(set_rx_freq, "uhd_usrp_set_rx_freq");
        get(get_rx_freq, "uhd_usrp_get_rx_freq");
        get(set_rx_gain, "uhd_usrp_set_rx_gain");
        get(get_rx_gain, "uhd_usrp_get_rx_gain");
        get(set_rx_antenna, "uhd_usrp_set_rx_antenna");
        get(get_tx_rate, "uhd_usrp_get_tx_rate");
        get(get_tx_freq, "uhd_usrp_get_tx_freq");
        get(get_tx_gain, "uhd_usrp_get_tx_gain");
        if (!missing.isEmpty()) {
            *error = QStringLiteral("%1 lacks %2").arg(library, missing.join(", "));
            return false;
        }
        const auto check = [&](uhd_error e, const char* what) {
            if (e == 0) return true;
            char text[1024] = {0};
            if (usrp_) usrp_last_error(usrp_, text, sizeof(text));
            *error = QStringLiteral("%1: %2").arg(QLatin1String(what), QString::fromLocal8Bit(text).trimmed());
            return false;
        };
        if (!check(usrp_make(&usrp_, args.toLocal8Bit().constData()), "Opening the USRP to receive")) {
            usrp_ = nullptr;
            return false;
        }
        if (!check(set_rx_rate(usrp_, rate, 0), "Setting the receive rate")) return false;
        get_rx_rate(usrp_, 0, &rate_);
        QByteArray tuneArgs("");
        TuneRequest req{kL1, kPolicyAuto, 0, kPolicyAuto, 0, tuneArgs.data()};
        TuneResult res{};
        if (!check(set_rx_freq(usrp_, &req, 0, &res), "Tuning the receiver")) return false;
        if (!check(set_rx_gain(usrp_, gainDb, 0, ""), "Setting the receive gain")) return false;
        if (!check(set_rx_antenna(usrp_, antenna.toLocal8Bit().constData(), 0), "Selecting the receive antenna")) return false;
        if (!check(rx_streamer_make(&streamer_), "Creating the receive streamer")) return false;
        if (!check(rx_metadata_make(&md_), "Creating the receive metadata")) return false;
        QByteArray cpu("sc16"), otw("sc16"), sargs("");
        size_t channel = 0;
        StreamArgs sa{cpu.data(), otw.data(), sargs.data(), &channel, 1};
        if (!check(get_rx_stream(usrp_, &sa, streamer_), "Opening the receive stream")) return false;
        if (!check(max_num_samps(streamer_, &maxSamps_), "Reading the packet size")) return false;
        return true;
    }

    double rate() const { return rate_; }

    // Both halves as the device has them now, which also shows that opening it twice changed nothing
    QString description() const
    {
        double rxFreq = 0, rxGain = 0, txRate = 0, txFreq = 0, txGain = 0;
        get_rx_freq(usrp_, 0, &rxFreq);
        get_rx_gain(usrp_, 0, "", &rxGain);
        get_tx_rate(usrp_, 0, &txRate);
        get_tx_freq(usrp_, 0, &txFreq);
        get_tx_gain(usrp_, 0, "", &txGain);
        return QStringLiteral("Device now: TX %1 MS/s at %2 MHz gain %3 dB, RX %4 MS/s at %5 MHz gain %6 dB")
            .arg(txRate / 1e6, 0, 'f', 6).arg(txFreq / 1e6, 0, 'f', 6).arg(txGain, 0, 'f', 1)
            .arg(rate_ / 1e6, 0, 'f', 6).arg(rxFreq / 1e6, 0, 'f', 6).arg(rxGain, 0, 'f', 1);
    }

    // Streams into the sink until stop is set. Returns an error message, empty if stopped.
    QString run(IqSink& sink)
    {
        StreamCmd cmd{kStreamStart, 0, true, 0, 0.0};
        if (issue_stream_cmd(streamer_, &cmd) != 0) return QStringLiteral("Starting the receive stream failed");
        const size_t chunk = std::max<size_t>(maxSamps_, 1) * 8;
        std::vector<int16_t> buf(chunk * 2);
        void* buffs[1] = {buf.data()};
        QString error;
        double sum = 0;
        int peak = 0;
        qint64 counted = 0;
        while (!stop.load() && error.isEmpty()) {
            size_t got = 0;
            if (rx_recv(streamer_, buffs, chunk, &md_, 0.5, false, &got) != 0) {
                error = QStringLiteral("Receiving failed");
                break;
            }
            int code = kMdNone;
            md_error_code(md_, &code);
            if (code == kMdOverflow) overflows.fetch_add(1);
            else if (code != kMdNone && code != kMdTimeout) error = QStringLiteral("Receive error, metadata code 0x%1").arg(code, 0, 16);
            if (got == 0) continue;
            for (size_t k = 0; k < 2 * got; ++k) {
                sum += double(buf[k]) * buf[k];
                peak = std::max(peak, std::abs(int(buf[k])));
            }
            counted += qint64(got);
            if (counted >= qint64(rate_)) {      // once a second, per rail, relative to full scale
                rmsDbfs = float(10.0 * std::log10(std::max(1e-12, sum / double(2 * counted)) / (32768.0 * 32768.0)));
                peakDbfs = float(20.0 * std::log10(std::max(1, peak) / 32768.0));
                sum = 0;
                peak = 0;
                counted = 0;
            }
            if (!sink.write(reinterpret_cast<const char*>(buf.data()), qint64(got) * 4)) error = sink.error();
        }
        StreamCmd halt{kStreamStop, 0, true, 0, 0.0};
        issue_stream_cmd(streamer_, &halt);
        return error;
    }

    std::atomic<float> rmsDbfs{-200}, peakDbfs{-200};
    std::atomic<int> overflows{0};

private:
    QLibrary lib_;
    void* usrp_ = nullptr;
    void* streamer_ = nullptr;
    void* md_ = nullptr;
    size_t maxSamps_ = 0;
    double rate_ = 0;
    uhd_error (*usrp_make)(void**, const char*) = nullptr;
    uhd_error (*usrp_free)(void**) = nullptr;
    uhd_error (*usrp_last_error)(void*, char*, size_t) = nullptr;
    uhd_error (*rx_streamer_make)(void**) = nullptr;
    uhd_error (*rx_streamer_free)(void**) = nullptr;
    uhd_error (*rx_metadata_make)(void**) = nullptr;
    uhd_error (*rx_metadata_free)(void**) = nullptr;
    uhd_error (*md_error_code)(void*, int*) = nullptr;
    uhd_error (*get_rx_stream)(void*, StreamArgs*, void*) = nullptr;
    uhd_error (*max_num_samps)(void*, size_t*) = nullptr;
    uhd_error (*rx_recv)(void*, void**, size_t, void**, double, bool, size_t*) = nullptr;
    uhd_error (*issue_stream_cmd)(void*, const StreamCmd*) = nullptr;
    uhd_error (*set_rx_rate)(void*, double, size_t) = nullptr;
    uhd_error (*get_rx_rate)(void*, size_t, double*) = nullptr;
    uhd_error (*set_rx_freq)(void*, TuneRequest*, size_t, TuneResult*) = nullptr;
    uhd_error (*get_rx_freq)(void*, size_t, double*) = nullptr;
    uhd_error (*set_rx_gain)(void*, double, size_t, const char*) = nullptr;
    uhd_error (*get_rx_gain)(void*, size_t, const char*, double*) = nullptr;
    uhd_error (*set_rx_antenna)(void*, const char*, size_t) = nullptr;
    uhd_error (*get_tx_rate)(void*, size_t, double*) = nullptr;
    uhd_error (*get_tx_freq)(void*, size_t, double*) = nullptr;
    uhd_error (*get_tx_gain)(void*, size_t, const char*, double*) = nullptr;
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    Scenario s;
    s.format = SampleFormat::Int16IQ;
    s.seconds = 0;
    s.fs = 2.5e6;
    UsrpSettings tx;
    double rxGain = 0;
    QString rxAntenna = QStringLiteral("RX2"), record;
    int port = 5555;
    for (int i = 1; i + 1 < args.size(); i += 2) {
        const QString& k = args[i];
        const QString& v = args[i + 1];
        if (k == "--usrp-args") tx.args = v;
        else if (k == "--tx-gain") tx.gainDb = v.toDouble();
        else if (k == "--rx-gain") rxGain = v.toDouble();
        else if (k == "--rx-antenna") rxAntenna = v;
        else if (k == "--port") port = v.toInt();
        else if (k == "--record") record = v;
        else if (k == "--seconds") s.seconds = v.toDouble();
        else if (k == "--fs") s.fs = v.toDouble();
        else if (k == "--cn0") s.cn0 = v.toDouble();
        else if (k == "--uhd-library") tx.library = v;
        else {
            std::printf("Unknown option %s\n", qPrintable(k));
            return 1;
        }
    }
    if (port == 0 && record.isEmpty()) {
        std::printf("Give --port and/or --record\n");
        return 1;
    }
    std::signal(SIGINT, onInterrupt);

    UsrpSink usrp;
    if (!usrp.open(tx, s)) {
        std::printf("%s\n", qPrintable(usrp.error()));
        return 2;
    }
    std::printf("%s\n", qPrintable(usrp.description()));
    Receiver rx;
    QString error;
    if (!rx.open(usrp.library(), tx.args, s.fs, rxGain, rxAntenna, &error)) {
        std::printf("%s\n", qPrintable(error));
        return 2;
    }
    std::printf("%s\n", qPrintable(rx.description()));

    // The receive side runs on its own thread, where its sockets live
    std::atomic<int> state{0};      // 1 = ready, 2 = gave up
    std::atomic<bool> transmitting{false};
    QString rxError;
    QThread* thread = QThread::create([&] {
        TeeSink tee;
        FileSink file;
        TcpSink tcp;
        if (!record.isEmpty()) {
            if (!file.open(record)) {
                rxError = file.error();
                state = 2;
                return;
            }
            tee.add(&file);
        }
        if (port) {
            Scenario header = s;      // what comes back sits at L1 itself, at the receive rate
            header.ifFreq = 0;
            header.fs = rx.rate();
            if (!tcp.listen(quint16(port))) {
                rxError = tcp.error();
                state = 2;
                return;
            }
            std::printf("Waiting for the receiver on port %d\n", port);
            std::fflush(stdout);
            if (!tcp.waitForClient(header, stop)) {
                state = 2;
                return;
            }
            std::printf("Receiver connected\n");
            tee.add(&tcp);
        }
        state = 1;
        // Receiving before the signal goes out would hand the receiver noise to search first
        while (!transmitting.load() && !stop.load()) QThread::msleep(1);
        rxError = rx.run(tee);
        stop = true;      // without the receive side there is nothing to see
    });
    thread->start(QThread::TimeCriticalPriority);
    while (state.load() == 0) QThread::msleep(10);

    if (state.load() == 1) {
        std::printf("Transmitting through the cable\n");
        error = runSimulation(s, usrp, stop, [&](const Progress& p) {
            transmitting = true;      // called once the first samples are out
            std::printf("\r%.1f s  %.1fx  %d sats  TX underflows %d  RX %.1f dBFS rms, peak %.1f dBFS, overflows %d   ",
                        p.seconds, p.speed, int(p.sats.size()), usrp.underflows(), double(rx.rmsDbfs.load()),
                        double(rx.peakDbfs.load()), rx.overflows.load());
            std::fflush(stdout);
        });
    }
    stop = true;
    thread->wait();
    delete thread;
    std::printf("\n");
    if (!rxError.isEmpty()) std::printf("Receive side: %s\n", qPrintable(rxError));
    if (!error.isEmpty()) std::printf("Transmit side: %s\n", qPrintable(error));
    std::printf("TX underflows %d, lost packets %d; RX overflows %d\n", usrp.underflows(), usrp.lostPackets(), rx.overflows.load());
    return error.isEmpty() && rxError.isEmpty() ? 0 : 3;
}
