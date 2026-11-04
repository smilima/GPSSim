#include "HackRfOutput.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sim {

namespace {

// ---------------------------------------------------------------------------
// The part of libhackrf's hackrf.h used here. It has kept these since 2018.
// ---------------------------------------------------------------------------
constexpr int kSuccess = 0, kTrue = 1;

struct Transfer
{
    void* device;
    uint8_t* buffer;
    int buffer_length;
    int valid_length;
    void* rx_ctx;
    void* tx_ctx;
};

// Half of it is in hand when the transmission starts, more than the four 256 kB transfers libhackrf asks for at once
// even at 2 MS/s
constexpr double kRingSeconds = 1.0;

bool loadLibrary(QLibrary& lib, const QString& configured, QString* tried)
{
    // A library that was asked for is the only one tried: falling back to another would reach a radio
    QStringList candidates;
    if (!configured.isEmpty())
        candidates << configured;
    else
        candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hackrf.dll")) << QStringLiteral("hackrf");
    for (const QString& c : candidates) {
        const QFileInfo fi(c);
        if (fi.isAbsolute() && !fi.exists() && c != configured) continue;
#ifdef Q_OS_WIN
        // libusb-1.0.dll sits next to it
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

struct HackRfSink::Api
{
    QLibrary lib;
    int (*init)() = nullptr;
    int (*exit)() = nullptr;
    int (*open_by_serial)(const char*, void**) = nullptr;
    int (*close)(void*) = nullptr;
    int (*start_tx)(void*, int (*)(void*), void*) = nullptr;
    int (*stop_tx)(void*) = nullptr;
    int (*is_streaming)(void*) = nullptr;
    int (*set_freq)(void*, uint64_t) = nullptr;
    int (*set_sample_rate)(void*, double) = nullptr;
    int (*set_baseband_filter_bandwidth)(void*, uint32_t) = nullptr;
    uint32_t (*compute_baseband_filter_bw)(uint32_t) = nullptr;
    int (*set_amp_enable)(void*, uint8_t) = nullptr;
    int (*set_txvga_gain)(void*, uint32_t) = nullptr;
    int (*set_antenna_enable)(void*, uint8_t) = nullptr;
    const char* (*error_name)(int) = nullptr;
    int (*board_id_read)(void*, uint8_t*) = nullptr;
    const char* (*board_id_name)(int) = nullptr;
    int (*version_string_read)(void*, char*, uint8_t) = nullptr;
    int (*get_clkin_status)(void*, uint8_t*) = nullptr;     // optional, like the one below
    const char* (*library_version)() = nullptr;
};

HackRfSink::HackRfSink() : api_(new Api) {}

HackRfSink::~HackRfSink()
{
    if (streaming_) {
        // Let the last samples go out, then stop
        std::unique_lock<std::mutex> hold(lock_);
        drained_.wait_for(hold, std::chrono::seconds(1), [this] { return held_ == 0; });
        hold.unlock();
        api_->stop_tx(device_);
    }
    if (device_) {
        api_->set_amp_enable(device_, 0);      // leave the HackRF quiet
        api_->set_txvga_gain(device_, 0);
        api_->close(device_);
    }
    if (initialised_) api_->exit();
}

QString HackRfSink::failure(const QString& what, int code) const
{
    const char* name = api_->error_name ? api_->error_name(code) : nullptr;
    return QStringLiteral("%1: %2").arg(what, name ? QString::fromLatin1(name) : QStringLiteral("error %1").arg(code));
}

bool HackRfSink::open(const HackRfSettings& h, const Scenario& s)
{
    if (!isComplex(s.format)) {
        error_ = QStringLiteral("Transmitting needs an I/Q format");
        return false;
    }
    if (s.fs < 2e6 || s.fs > 20e6) {
        error_ = QStringLiteral("A HackRF transmits at 2 to 20 MS/s");
        return false;
    }
    wide_ = s.format == SampleFormat::Int16IQ;

    QString tried;
    if (!loadLibrary(api_->lib, h.library, &tried)) {
        error_ = QStringLiteral("hackrf.dll was not found. Put it and libusb-1.0.dll next to the program (see the README).%1").arg(tried);
        return false;
    }
    QStringList missing;
    const auto get = [&](auto& fn, const char* name, bool required = true) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(api_->lib.resolve(name));
        if (!fn && required) missing << QLatin1String(name);
    };
    Api& a = *api_;
    get(a.init, "hackrf_init");
    get(a.exit, "hackrf_exit");
    get(a.open_by_serial, "hackrf_open_by_serial");
    get(a.close, "hackrf_close");
    get(a.start_tx, "hackrf_start_tx");
    get(a.stop_tx, "hackrf_stop_tx");
    get(a.is_streaming, "hackrf_is_streaming");
    get(a.set_freq, "hackrf_set_freq");
    get(a.set_sample_rate, "hackrf_set_sample_rate");
    get(a.set_baseband_filter_bandwidth, "hackrf_set_baseband_filter_bandwidth");
    get(a.compute_baseband_filter_bw, "hackrf_compute_baseband_filter_bw");
    get(a.set_amp_enable, "hackrf_set_amp_enable");
    get(a.set_txvga_gain, "hackrf_set_txvga_gain");
    get(a.set_antenna_enable, "hackrf_set_antenna_enable");
    get(a.error_name, "hackrf_error_name");
    get(a.board_id_read, "hackrf_board_id_read");
    get(a.board_id_name, "hackrf_board_id_name");
    get(a.version_string_read, "hackrf_version_string_read");
    get(a.get_clkin_status, "hackrf_get_clkin_status", false);
    get(a.library_version, "hackrf_library_version", false);
    if (!missing.isEmpty()) {
        error_ = QStringLiteral("%1 lacks %2. Is it libhackrf 2018 or later?").arg(a.lib.fileName(), missing.join(", "));
        return false;
    }

    int e = a.init();
    if (e != kSuccess) {
        error_ = failure(QStringLiteral("Starting libhackrf"), e);
        return false;
    }
    initialised_ = true;
    const QByteArray serial = h.serial.trimmed().toLatin1();
    e = a.open_by_serial(serial.isEmpty() ? nullptr : serial.constData(), &device_);
    if (e != kSuccess) {
        device_ = nullptr;
        error_ = failure(serial.isEmpty() ? QStringLiteral("Opening the HackRF") : QStringLiteral("Opening HackRF %1").arg(h.serial), e);
        return false;
    }

    bool clkin = false;
    uint8_t status = 0;
    if (a.get_clkin_status && a.get_clkin_status(device_, &status) == kSuccess) clkin = status != 0;
    if (h.externalRef && !clkin) {
        error_ = QStringLiteral("There is no 10 MHz signal on the HackRF's CLKIN");
        return false;
    }

    // Asking for less makes a fast crystal give the exact carrier and sample rate
    const double scale = 1.0 + h.ppm * 1e-6;
    const auto check = [&](int code, const QString& what) {
        if (code == kSuccess) return true;
        error_ = failure(what, code);
        return false;
    };
    if (!check(a.set_sample_rate(device_, s.fs / scale), QStringLiteral("Setting the sample rate"))) return false;
    // After the rate, which picks a filter of its own; at least the C/A main lobe
    const uint32_t filter = a.compute_baseband_filter_bw(uint32_t(std::max(0.75 * s.fs, 2.5e6)));
    if (!check(a.set_baseband_filter_bandwidth(device_, filter), QStringLiteral("Setting the filter"))) return false;
    const uint64_t freq = uint64_t(std::llround((kL1 - s.ifFreq) / scale));
    if (!check(a.set_freq(device_, freq), QStringLiteral("Tuning"))) return false;
    const uint32_t gain = uint32_t(std::clamp(std::lround(h.gainDb), 0L, 47L));
    if (!check(a.set_txvga_gain(device_, gain), QStringLiteral("Setting the gain"))) return false;
    if (!check(a.set_amp_enable(device_, h.amp ? 1 : 0), QStringLiteral("Setting the amplifier"))) return false;
    if (!check(a.set_antenna_enable(device_, 0), QStringLiteral("Turning off the antenna power"))) return false;

    ring_.assign(size_t(s.fs * kRingSeconds) * 2, 0);

    uint8_t board = 0;
    a.board_id_read(device_, &board);
    char version[255] = {0};
    a.version_string_read(device_, version, sizeof(version) - 1);
    const char* lib = a.library_version ? a.library_version() : "";
    description_ = QStringLiteral("%1, firmware %2: %3 MS/s at %4 MHz, TX gain %5 dB, amplifier %6, correction %7 ppm%8 "
                                  "(libhackrf %9)")
                       .arg(QString::fromLatin1(a.board_id_name(board)), QString::fromLatin1(version))
                       .arg(s.fs / 1e6, 0, 'f', 6).arg((kL1 - s.ifFreq) / 1e6, 0, 'f', 6).arg(gain)
                       .arg(h.amp ? QStringLiteral("on (+14 dB)") : QStringLiteral("off"))
                       .arg(h.ppm, 0, 'f', 2)
                       .arg(clkin ? QStringLiteral(", locked to CLKIN") : QString(), QString::fromLatin1(lib));
    return true;
}

bool HackRfSink::write(const char* data, qint64 size)
{
    const auto* in = reinterpret_cast<const int8_t*>(data);
    size_t bytes = size_t(size);
    if (wide_) {      // the HackRF takes 8 bits
        const auto* wide = reinterpret_cast<const int16_t*>(data);
        bytes = size_t(size) / 2;
        converted_.resize(bytes);
        for (size_t k = 0; k < bytes; ++k) converted_[k] = int8_t(std::clamp((int(wide[k]) + 128) >> 8, -127, 127));
        in = converted_.data();
    }

    size_t done = 0;
    while (done < bytes) {
        std::unique_lock<std::mutex> hold(lock_);
        if (!drained_.wait_for(hold, std::chrono::seconds(2), [this] { return held_ < ring_.size(); })) {
            error_ = QStringLiteral("The HackRF stopped taking samples");
            return false;
        }
        const size_t n = std::min(bytes - done, ring_.size() - held_);
        const size_t at = (readAt_ + held_) % ring_.size(), first = std::min(n, ring_.size() - at);
        std::memcpy(ring_.data() + at, in + done, first);
        std::memcpy(ring_.data(), in + done + first, n - first);
        held_ += n;
        done += n;
        const bool ready = !streaming_ && held_ >= ring_.size() / 2;      // start with something in hand
        hold.unlock();
        if (ready) {
            const int e = api_->start_tx(device_, &HackRfSink::onTransfer, this);
            if (e != kSuccess) {
                error_ = failure(QStringLiteral("Starting to transmit"), e);
                return false;
            }
            streaming_ = true;
        }
    }
    if (streaming_ && api_->is_streaming(device_) != kTrue) {
        error_ = QStringLiteral("The HackRF stopped transmitting. Was it unplugged?");
        return false;
    }
    return true;
}

// On the HackRF's thread: hands it the next buffer's worth, or silence if the simulation is behind
int HackRfSink::onTransfer(void* transfer)
{
    auto* t = static_cast<Transfer*>(transfer);
    auto* self = static_cast<HackRfSink*>(t->tx_ctx);
    const size_t want = size_t(t->buffer_length);
    {
        std::lock_guard<std::mutex> hold(self->lock_);
        const size_t size = self->ring_.size(), n = std::min(want, self->held_);
        const size_t first = std::min(n, size - self->readAt_);
        std::memcpy(t->buffer, self->ring_.data() + self->readAt_, first);
        std::memcpy(t->buffer + first, self->ring_.data(), n - first);
        if (n < want) {
            std::memset(t->buffer + n, 0, want - n);
            ++self->underflows_;
        }
        self->readAt_ = (self->readAt_ + n) % size;
        self->held_ -= n;
    }
    t->valid_length = int(want);
    self->drained_.notify_one();
    return 0;
}

} // namespace sim
