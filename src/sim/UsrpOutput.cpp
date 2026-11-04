#include "UsrpOutput.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QThread>

#include <cmath>
#include <cstdint>

namespace sim {

namespace {

// ---------------------------------------------------------------------------
// The part of the UHD C API used here, from uhd/usrp/usrp.h, uhd/types/tune_request.h, tune_result.h, metadata.h,
// sensors.h and usrp_info.h. These layouts are the same from UHD 3.15 up to the current 4.x releases, which is what
// allows a program to use the driver without being built against it. SoftGpsReceiver's UhdInput.cpp uses the same.
// ---------------------------------------------------------------------------
using uhd_error = int;
constexpr uhd_error kUhdNoError = 0;

enum { kPolicyAuto = 65 };                                     // uhd_tune_request_policy_t
enum {                                                          // uhd_async_metadata_event_code_t
    kEventUnderflow = 0x2, kEventSeqError = 0x4, kEventUnderflowInPacket = 0x10, kEventSeqErrorInBurst = 0x20
};

struct StreamArgs
{
    char* cpu_format;
    char* otw_format;
    char* args;
    size_t* channel_list;
    int n_channels;
};

struct TuneRequest
{
    double target_freq;
    int rf_freq_policy;
    double rf_freq;
    int dsp_freq_policy;
    double dsp_freq;
    char* args;
};

struct TuneResult
{
    double clipped_rf_freq;
    double target_rf_freq;
    double actual_rf_freq;
    double target_dsp_freq;
    double actual_dsp_freq;
};

struct TxInfo
{
    char* mboard_id;
    char* mboard_name;
    char* mboard_serial;
    char* tx_id;
    char* tx_subdev_name;
    char* tx_subdev_spec;
    char* tx_serial;
    char* tx_antenna;
};

bool loadLibrary(QLibrary& lib, const QString& configured, QString* tried)
{
    // A library that was asked for is the only one tried: falling back to the installed driver would reach a radio
    QStringList candidates;
    if (!configured.isEmpty()) {
        candidates << configured;
    } else {
        const QByteArray pkg = qgetenv("UHD_PKG_PATH");
        if (!pkg.isEmpty()) candidates << QDir(QString::fromLocal8Bit(pkg)).filePath(QStringLiteral("bin/uhd.dll"));
#ifdef Q_OS_WIN
        candidates << QStringLiteral("C:/Program Files/UHD/bin/uhd.dll") << QStringLiteral("C:/Program Files (x86)/UHD/bin/uhd.dll");
#endif
        candidates << QStringLiteral("uhd");     // the system search path: PATH, or libuhd.so on Linux
    }
    for (const QString& c : candidates) {
        const QFileInfo fi(c);
        if (fi.isAbsolute() && !fi.exists() && c != configured) continue;
#ifdef Q_OS_WIN
        // uhd.dll's own dependencies sit next to it
        if (fi.isAbsolute())
            qputenv("PATH", QDir::toNativeSeparators(fi.absolutePath()).toLocal8Bit() + ';' + qgetenv("PATH"));
#endif
        lib.setFileName(c);
        if (lib.load()) return true;
        tried->append(QStringLiteral("\n  %1: %2").arg(c, lib.errorString()));
    }
    return false;
}

} // namespace

struct UsrpSink::Api
{
    QLibrary lib;
    uhd_error (*usrp_make)(void**, const char*) = nullptr;
    uhd_error (*usrp_free)(void**) = nullptr;
    uhd_error (*get_last_error)(char*, size_t) = nullptr;
    uhd_error (*usrp_last_error)(void*, char*, size_t) = nullptr;
    uhd_error (*tx_streamer_make)(void**) = nullptr;
    uhd_error (*tx_streamer_free)(void**) = nullptr;
    uhd_error (*tx_metadata_make)(void**, bool, int64_t, double, bool, bool) = nullptr;
    uhd_error (*tx_metadata_free)(void**) = nullptr;
    uhd_error (*async_metadata_make)(void**) = nullptr;
    uhd_error (*async_metadata_free)(void**) = nullptr;
    uhd_error (*async_event_code)(void*, int*) = nullptr;
    uhd_error (*get_tx_stream)(void*, StreamArgs*, void*) = nullptr;
    uhd_error (*tx_send)(void*, const void**, size_t, void**, double, size_t*) = nullptr;
    uhd_error (*tx_recv_async_msg)(void*, void**, double, bool*) = nullptr;
    uhd_error (*set_tx_rate)(void*, double, size_t) = nullptr;
    uhd_error (*get_tx_rate)(void*, size_t, double*) = nullptr;
    uhd_error (*set_tx_freq)(void*, TuneRequest*, size_t, TuneResult*) = nullptr;
    uhd_error (*get_tx_freq)(void*, size_t, double*) = nullptr;
    uhd_error (*set_tx_gain)(void*, double, size_t, const char*) = nullptr;
    uhd_error (*get_tx_gain)(void*, size_t, const char*, double*) = nullptr;
    uhd_error (*set_clock_source)(void*, const char*, size_t) = nullptr;
    uhd_error (*get_mboard_sensor)(void*, const char*, size_t, void**) = nullptr;
    uhd_error (*sensor_value_make)(void**) = nullptr;
    uhd_error (*sensor_value_free)(void**) = nullptr;
    uhd_error (*sensor_value_to_bool)(void*, bool*) = nullptr;
    uhd_error (*get_tx_info)(void*, size_t, TxInfo*) = nullptr;   // optional, like the two below
    uhd_error (*tx_info_free)(TxInfo*) = nullptr;
    uhd_error (*get_version_string)(char*, size_t) = nullptr;
};

UsrpSink::UsrpSink() : api_(new Api) {}

UsrpSink::~UsrpSink()
{
    if (started_) {      // end the burst, so the device stops cleanly rather than reporting an underflow
        void* last = nullptr;
        if (api_->tx_metadata_make(&last, false, 0, 0.0, false, true) == kUhdNoError) {
            const int16_t none[2] = {0, 0};
            const void* buffs[1] = {none};
            size_t sent = 0;
            api_->tx_send(streamer_, buffs, 0, &last, 1.0, &sent);
            api_->tx_metadata_free(&last);
        }
    }
    if (usrp_ && api_->set_tx_gain) api_->set_tx_gain(usrp_, 0.0, 0, "");      // the device keeps its settings: leave it quiet
    if (events_) api_->async_metadata_free(&events_);
    if (metadata_) api_->tx_metadata_free(&metadata_);
    if (streamer_) api_->tx_streamer_free(&streamer_);
    if (usrp_) api_->usrp_free(&usrp_);
}

QString UsrpSink::library() const
{
    return api_->lib.fileName();
}

QString UsrpSink::lastError(int code) const
{
    char text[1024] = {0};
    if (usrp_ && api_->usrp_last_error) api_->usrp_last_error(usrp_, text, sizeof(text));
    if (!text[0] && api_->get_last_error) api_->get_last_error(text, sizeof(text));
    const QString s = QString::fromLocal8Bit(text).trimmed();
    return s.isEmpty() ? QStringLiteral("UHD error %1").arg(code) : s;
}

bool UsrpSink::open(const UsrpSettings& u, const Scenario& s)
{
    if (!isComplex(s.format)) {
        error_ = QStringLiteral("Transmitting needs an I/Q format");
        return false;
    }
    wide_ = s.format == SampleFormat::Int16IQ;

    QString tried;
    if (!loadLibrary(api_->lib, u.library, &tried)) {
        error_ = QStringLiteral("The UHD driver was not found. Install UHD from https://files.ettus.com/binaries/uhd/.%1").arg(tried);
        return false;
    }
    QStringList missing;
    const auto get = [&](auto& fn, const char* name, bool required = true) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(api_->lib.resolve(name));
        if (!fn && required) missing << QLatin1String(name);
    };
    Api& a = *api_;
    get(a.usrp_make, "uhd_usrp_make");
    get(a.usrp_free, "uhd_usrp_free");
    get(a.get_last_error, "uhd_get_last_error");
    get(a.usrp_last_error, "uhd_usrp_last_error");
    get(a.tx_streamer_make, "uhd_tx_streamer_make");
    get(a.tx_streamer_free, "uhd_tx_streamer_free");
    get(a.tx_metadata_make, "uhd_tx_metadata_make");
    get(a.tx_metadata_free, "uhd_tx_metadata_free");
    get(a.async_metadata_make, "uhd_async_metadata_make");
    get(a.async_metadata_free, "uhd_async_metadata_free");
    get(a.async_event_code, "uhd_async_metadata_event_code");
    get(a.get_tx_stream, "uhd_usrp_get_tx_stream");
    get(a.tx_send, "uhd_tx_streamer_send");
    get(a.tx_recv_async_msg, "uhd_tx_streamer_recv_async_msg");
    get(a.set_tx_rate, "uhd_usrp_set_tx_rate");
    get(a.get_tx_rate, "uhd_usrp_get_tx_rate");
    get(a.set_tx_freq, "uhd_usrp_set_tx_freq");
    get(a.get_tx_freq, "uhd_usrp_get_tx_freq");
    get(a.set_tx_gain, "uhd_usrp_set_tx_gain");
    get(a.get_tx_gain, "uhd_usrp_get_tx_gain");
    get(a.set_clock_source, "uhd_usrp_set_clock_source");
    get(a.get_mboard_sensor, "uhd_usrp_get_mboard_sensor");
    get(a.sensor_value_make, "uhd_sensor_value_make");
    get(a.sensor_value_free, "uhd_sensor_value_free");
    get(a.sensor_value_to_bool, "uhd_sensor_value_to_bool");
    get(a.get_tx_info, "uhd_usrp_get_tx_info", false);
    get(a.tx_info_free, "uhd_usrp_tx_info_free", false);
    get(a.get_version_string, "uhd_get_version_string", false);
    if (!missing.isEmpty()) {
        error_ = QStringLiteral("%1 lacks %2. Is it a UHD 3.15 or later library?").arg(a.lib.fileName(), missing.join(", "));
        return false;
    }

    const auto check = [&](uhd_error e, const QString& what) {
        if (e == kUhdNoError) return true;
        error_ = QStringLiteral("%1: %2").arg(what, lastError(e));
        return false;
    };

    const QString name = u.args.isEmpty() ? QStringLiteral("(the first one found)") : u.args;
    if (!check(a.usrp_make(&usrp_, u.args.toLocal8Bit().constData()), QStringLiteral("Opening USRP %1").arg(name))) {
        usrp_ = nullptr;
        return false;
    }
    if (u.externalRef) {
        if (!check(a.set_clock_source(usrp_, "external", 0), QStringLiteral("Selecting the external reference"))) return false;
        bool locked = false;
        for (int i = 0; i < 20 && !locked; ++i) {      // the reference PLL takes a moment to settle
            void* sensor = nullptr;
            if (a.sensor_value_make(&sensor) != kUhdNoError) break;
            if (a.get_mboard_sensor(usrp_, "ref_locked", 0, &sensor) == kUhdNoError) a.sensor_value_to_bool(sensor, &locked);
            a.sensor_value_free(&sensor);
            if (!locked) QThread::msleep(100);
        }
        if (!locked) {
            error_ = QStringLiteral("The USRP did not lock to the external reference. Is there a 10 MHz signal on REF IN?");
            return false;
        }
    }

    double rate = 0;
    if (!check(a.set_tx_rate(usrp_, s.fs, 0), QStringLiteral("Setting the sample rate"))) return false;
    if (!check(a.get_tx_rate(usrp_, 0, &rate), QStringLiteral("Reading the sample rate"))) return false;
    if (std::fabs(rate - s.fs) > 0.5) {      // the signal would be generated at the wrong rate
        error_ = QStringLiteral("The USRP cannot transmit at %1 MS/s. The nearest rate it has is %2 MS/s: set the sample rate to that.")
                     .arg(s.fs / 1e6, 0, 'f', 6).arg(rate / 1e6, 0, 'f', 6);
        return false;
    }

    // The signal sits at the IF in the samples, so the centre goes that far below L1
    QByteArray tuneArgs("");
    TuneRequest req{};
    req.target_freq = kL1 - s.ifFreq;
    req.rf_freq_policy = kPolicyAuto;
    req.dsp_freq_policy = kPolicyAuto;
    req.args = tuneArgs.data();
    TuneResult res{};
    double freq = 0, gain = 0;
    if (!check(a.set_tx_freq(usrp_, &req, 0, &res), QStringLiteral("Tuning"))) return false;
    if (!check(a.get_tx_freq(usrp_, 0, &freq), QStringLiteral("Reading the tuned frequency"))) return false;
    if (!check(a.set_tx_gain(usrp_, u.gainDb, 0, ""), QStringLiteral("Setting the gain"))) return false;
    a.get_tx_gain(usrp_, 0, "", &gain);

    if (!check(a.tx_streamer_make(&streamer_), QStringLiteral("Creating the transmit streamer"))) return false;
    QByteArray cpu("sc16"), otw("sc16"), sargs("");
    size_t channel = 0;
    StreamArgs sa{cpu.data(), otw.data(), sargs.data(), &channel, 1};
    if (!check(a.get_tx_stream(usrp_, &sa, streamer_), QStringLiteral("Opening the transmit stream"))) return false;
    if (!check(a.tx_metadata_make(&metadata_, false, 0, 0.0, true, false), QStringLiteral("Creating the metadata"))) return false;
    if (!check(a.async_metadata_make(&events_), QStringLiteral("Creating the event metadata"))) return false;

    QString device = QStringLiteral("USRP");
    TxInfo info{};
    if (a.get_tx_info && a.tx_info_free && a.get_tx_info(usrp_, 0, &info) == kUhdNoError) {
        device = QStringLiteral("USRP %1, %2 %3 port").arg(QString::fromLocal8Bit(info.mboard_id), QString::fromLocal8Bit(info.tx_id),
                                                         QString::fromLocal8Bit(info.tx_antenna));
        a.tx_info_free(&info);
    }
    char version[128] = {0};
    if (a.get_version_string) a.get_version_string(version, sizeof(version));
    description_ = QStringLiteral("%1: %2 MS/s at %3 MHz, TX gain %4 dB%5 (UHD %6)")
                       .arg(device).arg(rate / 1e6, 0, 'f', 6).arg(freq / 1e6, 0, 'f', 6).arg(gain, 0, 'f', 1)
                       .arg(u.externalRef ? QStringLiteral(", external reference") : QString(), QString::fromLatin1(version));
    return true;
}

bool UsrpSink::write(const char* data, qint64 size)
{
    const size_t samples = size_t(size) / (wide_ ? 4 : 2);
    const int16_t* iq = reinterpret_cast<const int16_t*>(data);
    if (!wide_) {      // the device takes 16 bits
        buf_.resize(samples * 2);
        const auto* in = reinterpret_cast<const int8_t*>(data);
        for (size_t k = 0; k < samples * 2; ++k) buf_[k] = int16_t(in[k] * 256);
        iq = buf_.data();
    }

    size_t done = 0;
    int stalled = 0;
    while (done < samples) {
        const void* buffs[1] = {iq + 2 * done};
        size_t sent = 0;
        const uhd_error e = api_->tx_send(streamer_, buffs, samples - done, &metadata_, 1.0, &sent);
        if (e != kUhdNoError) {
            error_ = QStringLiteral("Transmitting: %1").arg(lastError(e));
            return false;
        }
        if (sent == 0 && ++stalled >= 5) {
            error_ = QStringLiteral("The USRP stopped taking samples");
            return false;
        }
        if (sent > 0 && !started_) {      // only the first packet starts the burst
            started_ = true;
            api_->tx_metadata_free(&metadata_);
            if (api_->tx_metadata_make(&metadata_, false, 0, 0.0, false, false) != kUhdNoError) {
                error_ = QStringLiteral("Creating the metadata: %1").arg(lastError(0));
                return false;
            }
        }
        done += sent;
    }
    readEvents();
    return true;
}

void UsrpSink::readEvents()
{
    bool valid = true;
    while (valid) {
        valid = false;
        if (api_->tx_recv_async_msg(streamer_, &events_, 0.0, &valid) != kUhdNoError || !valid) return;
        int code = 0;
        api_->async_event_code(events_, &code);
        if (code == kEventUnderflow || code == kEventUnderflowInPacket) ++underflows_;
        else if (code == kEventSeqError || code == kEventSeqErrorInBurst) ++lostPackets_;
    }
}

} // namespace sim
