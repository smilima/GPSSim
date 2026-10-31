#include "Simulator.h"

#include "Lnav.h"

#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QRegularExpression>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <random>

namespace sim {

namespace {

constexpr int kChunkMs = 50;              // samples produced at a time
constexpr int kNoiseSize = 1 << 22;
constexpr int kSineSize = 4096;

struct SineTable
{
    float cosine[kSineSize], sine[kSineSize];
    SineTable()
    {
        for (int i = 0; i < kSineSize; ++i) {
            cosine[i] = float(std::cos(2.0 * kPi * i / kSineSize));
            sine[i] = float(std::sin(2.0 * kPi * i / kSineSize));
        }
    }
};
const SineTable& sineTable()
{
    static const SineTable t;
    return t;
}

// Delay from the satellite clock to the receiver clock at one instant
struct Delay
{
    double code = 0;                      // receiver time minus satellite time, s
    double carrier = 0;                   // same for the carrier, advanced by the ionosphere
    double az = 0, el = 0;                // rad
};

struct Scene
{
    Scenario s;
    double lat = 0, lon = 0;
    Vec3 rx0, vel;
    Iono iono;
    std::vector<Ephemeris> sats;
};

Delay delayAt(const Scene& sc, const Ephemeris& e, double rel)
{
    const Scenario& s = sc.s;
    const double clock = s.clockBias + s.clockDrift * rel;
    const double tTrue = s.tow + rel - clock;      // the sample with this tag was taken at this GPS time
    const Vec3 rx{sc.rx0.x + sc.vel.x * rel, sc.rx0.y + sc.vel.y * rel, sc.rx0.z + sc.vel.z * rel};

    double flight = 0.075, r = 0;
    Vec3 p;
    for (int i = 0; i < 4; ++i) {
        const Vec3 q = satPosition(e, tTrue - flight);
        const double w = kOmegaE * flight, c = std::cos(w), sn = std::sin(w);
        p = {q.x * c + q.y * sn, -q.x * sn + q.y * c, q.z};      // into the frame of the moment of reception
        const double dx = p.x - rx.x, dy = p.y - rx.y, dz = p.z - rx.z;
        r = std::sqrt(dx * dx + dy * dy + dz * dz);
        flight = r / kC;
    }
    Delay d;
    azEl(sc.lat, sc.lon, rx, p, &d.az, &d.el);
    const double tropo = s.tropo ? tropoDelay(sc.lat, s.altM, d.el) : 0.0;
    const double iono = s.iono ? ionoDelay(sc.iono, sc.lat, sc.lon, d.az, d.el, tTrue - flight) : 0.0;
    const double dts = satClock(e, tTrue - flight);
    d.code = clock + flight + (tropo + iono) / kC - dts;
    d.carrier = clock + flight + (tropo - iono) / kC - dts;
    return d;
}

double cn0At(const Scenario& s, double elRad)
{
    return s.cn0 - 8.0 * (1.0 - std::sin(elRad));      // weaker towards the horizon
}

// One satellite's contribution, added in complex baseband or at the IF
class SatSignal
{
public:
    SatSignal(const Scene* sc, const Ephemeris& e)
        : sc_(sc), e_(e), code_(caCode(e.prn)), nav_(e, sc->sats, sc->iono)
    {
        for (int i = 0; i < 1023; ++i) table_[size_t(i)] = float(code_[size_t(i)]);
    }

    int prn() const { return e_.prn; }

    // Adds samples first..last-1 of the run, which fall within milliseconds firstMs.. of it
    void add(qint64 first, qint64 last, float* outI, float* outQ, double amplitude)
    {
        const Scenario& s = sc_->s;
        const SineTable& sines = sineTable();
        qint64 n = first;
        while (n < last) {
            qint64 ms = qint64(std::floor(double(n) / s.fs * 1e3));
            qint64 msEnd = qint64(std::ceil(double(ms + 1) * s.fs / 1e3));
            if (msEnd <= n) msEnd = qint64(std::ceil(double(++ms + 1) * s.fs / 1e3));      // rounding at a boundary
            const qint64 stop = std::min(last, msEnd);
            const Delay d0 = delayAt(*sc_, e_, double(ms) * 1e-3), d1 = delayAt(*sc_, e_, double(ms + 1) * 1e-3);
            const double rateCode = (d1.code - d0.code) * 1e3, rateCarrier = (d1.carrier - d0.carrier) * 1e3;

            const double rel = double(n) / s.fs;
            const double frac = rel - double(ms) * 1e-3;
            const double relSv = rel - (d0.code + rateCode * frac);
            double codeMs = std::floor(relSv * 1e3);
            double chip = (relSv * 1e3 - codeMs) * 1023.0;
            const double chipStep = kChipRate * (1.0 - rateCode) / s.fs;

            // A carrier of IF minus the L1 cycles still on their way
            double cycles = s.ifFreq * rel - kL1 * (d0.carrier + rateCarrier * frac);
            cycles -= std::floor(cycles);
            uint32_t phase = uint32_t(cycles * 4294967296.0);
            const uint32_t step = uint32_t(int64_t(std::llround((s.ifFreq - kL1 * rateCarrier) / s.fs * 4294967296.0)));

            float sign = bitSign(qint64(codeMs));
            const float a = float(amplitude);
            const bool complex = outQ != nullptr;
            for (; n < stop; ++n) {
                int idx = int(chip);
                if (idx >= 1023) {
                    chip -= 1023.0;
                    idx -= 1023;
                    codeMs += 1.0;
                    sign = bitSign(qint64(codeMs));
                }
                const float v = a * sign * table_[size_t(idx)];
                const uint32_t p = phase >> (32 - 12);
                outI[n - first] += v * sines.cosine[p];
                if (complex) outQ[n - first] += v * sines.sine[p];
                chip += chipStep;
                phase += step;
            }
        }
    }

private:
    float bitSign(qint64 relMs)
    {
        const qint64 ms = qint64(sc_->s.tow) * 1000 + relMs;
        return nav_.bit((ms + qint64(kSecondsPerWeek) * 1000) / 20) ? -1.0f : 1.0f;
    }

    const Scene* sc_;
    Ephemeris e_;
    std::array<int8_t, 1023> code_;
    std::array<float, 1023> table_{};
    LnavEncoder nav_;
};

} // namespace

QString Scenario::validate() const
{
    if (fs < 2.046e6) return QStringLiteral("The sample rate must be at least 2.046 MHz to hold the C/A main lobe.");
    if (fs > 100e6) return QStringLiteral("The sample rate is limited to 100 MHz.");
    if (isComplex(format)) {
        if (std::fabs(ifFreq) + 1.1e6 > fs / 2) return QStringLiteral("The IF puts the signal outside the sampled band.");
    } else if (ifFreq < 1.1e6 || ifFreq + 1.1e6 > fs / 2) {
        return QStringLiteral("Real samples need an IF that keeps the signal between 0 and half the sample rate.");
    }
    if (std::fabs(latDeg) > 90 || std::fabs(lonDeg) > 180) return QStringLiteral("Latitude or longitude out of range.");
    if (tow < 0 || tow >= kSecondsPerWeek) return QStringLiteral("The time of week must be from 0 to 604799 s.");
    if (noiseSigma <= 0) return QStringLiteral("The noise level must be above zero: it sets the signal level through C/N0.");
    return QString();
}

QString suggestedFileName(const Scenario& s)
{
    const auto mhz = [](double hz) { return QString::number(hz / 1e6, 'f', 6).remove(QRegularExpression("\\.?0+$")).replace('.', '_'); };
    const QString fmt = s.format == SampleFormat::Int8IQ ? QStringLiteral("int8iq")
                        : s.format == SampleFormat::Int16IQ ? QStringLiteral("int16iq")
                        : s.format == SampleFormat::Int8Real ? QStringLiteral("int8") : QStringLiteral("int16");
    const QString fs = mhz(s.fs), fif = mhz(std::fabs(s.ifFreq));
    return QStringLiteral("GPS_sim-fs%1-if%2-%3.bin").arg(fs, fif.isEmpty() ? QStringLiteral("0") : fif, fmt);
}

QString runSimulation(const Scenario& s, IqSink& sink, const std::atomic<bool>& stop,
                      const std::function<void(const Progress&)>& progress)
{
    const QString invalid = s.validate();
    if (!invalid.isEmpty()) return invalid;

    Scene sc;
    sc.s = s;
    sc.lat = s.latDeg * kPi / 180.0;
    sc.lon = s.lonDeg * kPi / 180.0;
    sc.rx0 = llhToEcef(sc.lat, sc.lon, s.altM);
    sc.vel = enuToEcef(sc.lat, sc.lon, s.velEast, s.velNorth, s.velUp);
    sc.iono = defaultIono();
    const int toe = std::clamp(int(std::round(s.tow / 7200.0)) * 7200, 0, 597600);
    sc.sats = makeConstellation(s.week, toe);

    std::vector<std::unique_ptr<SatSignal>> satSignals;
    for (const Ephemeris& e : sc.sats)
        if (e.prn != s.excludedPrn) satSignals.push_back(std::make_unique<SatSignal>(&sc, e));

    std::vector<float> noise(kNoiseSize);
    std::mt19937_64 rng(0x5eed + quint64(s.tow));
    std::normal_distribution<float> gauss;
    for (float& v : noise) v = gauss(rng);

    const bool complex = isComplex(s.format), wide = s.format == SampleFormat::Int16IQ || s.format == SampleFormat::Int16Real;
    const float scale = wide ? 256.0f : 1.0f, limit = wide ? 32767.0f : 127.0f;
    const float sigma = float(s.noiseSigma) * scale;
    // C/N0 is the carrier power over the noise density 2 sigma^2 / fs; a real signal keeps half the power of its amplitude
    const auto amplitude = [&](double cn0) {
        return std::sqrt(std::pow(10.0, cn0 / 10.0) * 2.0 * double(sigma) * double(sigma) / s.fs * (complex ? 1.0 : 2.0));
    };

    QThreadPool pool;
    if (s.threads > 0) pool.setMaxThreadCount(s.threads);

    struct Active
    {
        SatSignal* sat;
        double amplitude;
        std::vector<float> i, q;
    };
    std::vector<Active> active;
    std::vector<float> sumI, sumQ;
    std::vector<char> bytes;

    const qint64 total = s.seconds > 0 ? qint64(std::llround(s.seconds * s.fs)) : -1;
    const qint64 chunkSamples = std::max<qint64>(1, qint64(s.fs * kChunkMs / 1e3));
    QElapsedTimer wall;
    wall.start();
    qint64 written = 0, sent = 0, clipped = 0, lastSecond = -1, lastReport = -1;
    Progress report;
    while (!stop.load() && (total < 0 || written < total)) {
        const qint64 n = total < 0 ? chunkSamples : std::min(chunkSamples, total - written);
        const double rel = double(written) / s.fs;

        // The sky changes slowly: choose the satellites once a second
        if (qint64(rel) != lastSecond) {
            lastSecond = qint64(rel);
            active.clear();
            report.sats.clear();
            for (auto& sig : satSignals) {
                const Ephemeris& e = sc.sats[size_t(sig->prn() - 1)];
                const Delay d = delayAt(sc, e, rel), d2 = delayAt(sc, e, rel + 0.1);
                if (d.el * 180.0 / kPi < s.elevationMask) continue;
                const double cn0 = cn0At(s, d.el);
                active.push_back({sig.get(), amplitude(cn0), {}, {}});
                report.sats.push_back({e.prn, d.az * 180.0 / kPi, d.el * 180.0 / kPi, d.code * kC,
                                       -(d2.carrier - d.carrier) / 0.1 * kL1, cn0});
            }
        }

        sumI.assign(size_t(n), 0.f);
        sumQ.assign(complex ? size_t(n) : 0, 0.f);
        QtConcurrent::blockingMap(&pool, active, [&](Active& a) {
            a.i.assign(size_t(n), 0.f);
            a.q.assign(complex ? size_t(n) : 0, 0.f);
            a.sat->add(written, written + n, a.i.data(), complex ? a.q.data() : nullptr, a.amplitude);
        });
        for (const Active& a : active) {
            for (qint64 k = 0; k < n; ++k) sumI[size_t(k)] += a.i[size_t(k)];
            if (complex)
                for (qint64 k = 0; k < n; ++k) sumQ[size_t(k)] += a.q[size_t(k)];
        }

        size_t at = size_t(rng() % kNoiseSize);
        const auto next = [&]() {
            const float v = noise[at];
            if (++at == kNoiseSize) at = 0;
            return v * sigma;
        };
        bytes.resize(size_t(n) * size_t(bytesPerSample(s.format)));
        const auto convert = [&](float v) {
            v = std::round(v);
            if (std::fabs(v) > limit) {
                ++clipped;
                v = v > 0 ? limit : -limit;
            }
            return v;
        };
        if (wide) {
            auto* out = reinterpret_cast<int16_t*>(bytes.data());
            for (qint64 k = 0; k < n; ++k) {
                *out++ = int16_t(convert(sumI[size_t(k)] + next()));
                if (complex) *out++ = int16_t(convert(sumQ[size_t(k)] + next()));
            }
        } else {
            auto* out = reinterpret_cast<int8_t*>(bytes.data());
            for (qint64 k = 0; k < n; ++k) {
                *out++ = int8_t(convert(sumI[size_t(k)] + next()));
                if (complex) *out++ = int8_t(convert(sumQ[size_t(k)] + next()));
            }
        }

        if (s.realTime) {
            const double ahead = rel - double(wall.nsecsElapsed()) * 1e-9;
            if (ahead > 0.02 && !stop.load()) QThread::usleep(ulong(ahead * 1e6));
        }
        if (!sink.write(bytes.data(), qint64(bytes.size()))) return sink.error();
        written += n;
        sent += qint64(bytes.size());

        const qint64 tenth = qint64(double(written) / s.fs * 5.0);
        if (progress && (tenth != lastReport || written == total)) {
            lastReport = tenth;
            report.seconds = double(written) / s.fs;
            report.speed = report.seconds / std::max(1e-6, double(wall.nsecsElapsed()) * 1e-9);
            report.bytes = sent;
            report.clipped = double(clipped) / double(written * (complex ? 2 : 1));
            report.truth = {sc.rx0.x + sc.vel.x * report.seconds, sc.rx0.y + sc.vel.y * report.seconds, sc.rx0.z + sc.vel.z * report.seconds};
            progress(report);
        }
    }
    return QString();
}

} // namespace sim
