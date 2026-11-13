#include "Gps.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

double quantize(double v, int exponent)
{
    const double lsb = std::ldexp(1.0, exponent);
    return std::round(v / lsb) * lsb;
}

double wrapSemi(double v)
{
    return v - 2.0 * std::floor((v + 1.0) / 2.0);
}

double norm(const Vec3& a)
{
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

// Repeatable values in [-1, 1) per satellite and parameter
double spread(int prn, int which)
{
    uint32_t x = uint32_t(prn) * 2654435761u + uint32_t(which) * 40503u + 12345u;
    x ^= x >> 13;
    x *= 1274126177u;
    x ^= x >> 16;
    return double(x) / 2147483648.0 - 1.0;
}

double wrapWeek(double dt)
{
    if (dt > kSecondsPerWeek / 2) dt -= kSecondsPerWeek;
    else if (dt < -kSecondsPerWeek / 2) dt += kSecondsPerWeek;
    return dt;
}

double eccentricAnomaly(const Ephemeris& e, double t)
{
    const double a = e.sqrtA * e.sqrtA;
    const double n = std::sqrt(kMu / (a * a * a)) + e.deltaN * kGpsPi;
    const double mk = e.m0 * kGpsPi + n * wrapWeek(t - e.toe);
    double ek = mk;
    for (int i = 0; i < 12; ++i) ek = mk + e.e * std::sin(ek);
    return ek;
}

} // namespace

std::vector<Ephemeris> makeConstellation(int week, int toe)
{
    std::vector<Ephemeris> sats;
    for (int prn = 1; prn <= kNumSats; ++prn) {
        const int plane = (prn - 1) % 6, slot = (prn - 1) / 6;
        Ephemeris e;
        e.prn = prn;
        e.week = week;
        e.iodc = e.iode = (toe / 7200) % 200 + prn;
        e.toc = e.toe = toe;
        e.af0 = quantize(2e-4 * spread(prn, 1), -31);
        e.af1 = quantize(3e-12 * spread(prn, 2), -43);
        e.tgd = quantize(6e-9 * spread(prn, 3), -31);
        e.e = quantize(0.006 + 0.004 * spread(prn, 4), -33);
        e.sqrtA = quantize(5153.7 + 0.3 * spread(prn, 5), -19);
        e.deltaN = quantize(1.4e-9 + 2e-10 * spread(prn, 6), -43);
        e.omega = quantize(wrapSemi(spread(prn, 7)), -31);
        e.i0 = quantize(55.0 / 180.0 + 0.01 * spread(prn, 8), -31);
        e.iDot = quantize(1e-10 * spread(prn, 9), -43);
        e.omegaDot = quantize(-2.6e-9 + 1e-10 * spread(prn, 10), -43);
        e.omega0 = quantize(wrapSemi(plane / 3.0 + 0.01 * spread(prn, 11)), -31);
        // Equally spaced in the plane, staggered between planes. The argument of perigee is taken back out, so the
        // spacing holds in latitude rather than in anomaly.
        e.m0 = quantize(wrapSemi(2.0 * (slot / 6.0 + plane / 36.0) - e.omega), -31);
        e.crs = quantize(40.0 * spread(prn, 12), -5);
        e.crc = quantize(200.0 + 60.0 * spread(prn, 13), -5);
        e.cuc = quantize(2e-6 * spread(prn, 14), -29);
        e.cus = quantize(6e-6 * spread(prn, 15), -29);
        e.cic = quantize(1e-7 * spread(prn, 16), -29);
        e.cis = quantize(1e-7 * spread(prn, 17), -29);
        sats.push_back(e);
    }
    return sats;
}

std::vector<Ephemeris> ephemerisFromAlmanac(const std::vector<AlmanacEntry>& almanac, int week, int toe)
{
    std::vector<Ephemeris> sats(kNumSats);
    for (const AlmanacEntry& a : almanac) {
        if (a.prn < 1 || a.prn > kNumSats || a.health != 0) continue;
        const double dt = double(week - a.week) * kSecondsPerWeek + (toe - a.toa);      // from toa to toe
        const double n = std::sqrt(kMu / std::pow(a.sqrtA, 6));
        Ephemeris& e = sats[size_t(a.prn - 1)];
        e.prn = a.prn;
        e.week = week;
        e.iodc = e.iode = (toe / 7200) % 200 + a.prn;
        e.toc = e.toe = toe;
        e.af0 = quantize(std::clamp(a.af0 + a.af1 * dt, -9.7e-4, 9.7e-4), -31);      // the 22 bits of the message
        e.af1 = quantize(a.af1, -43);
        e.e = quantize(a.e, -33);
        e.sqrtA = quantize(a.sqrtA, -19);
        e.i0 = quantize(a.i0 / kGpsPi, -31);
        e.omegaDot = quantize(a.omegaDot / kGpsPi, -43);
        e.omega = quantize(wrapSemi(a.omega / kGpsPi), -31);
        // The node is measured from Greenwich at the start of the week, so a later week starts that much further round
        e.omega0 = quantize(wrapSemi((a.omega0 + a.omegaDot * dt - kOmegaE * double(week - a.week) * kSecondsPerWeek) / kGpsPi), -31);
        e.m0 = quantize(wrapSemi((a.m0 + n * dt) / kGpsPi), -31);
    }
    return sats;
}

Iono defaultIono()
{
    Iono io;
    const double a[4] = {1.1176e-8, 7.4506e-9, -5.9605e-8, -5.9605e-8};
    const double b[4] = {90112.0, 0.0, -196608.0, -65536.0};
    const int ea[4] = {-30, -27, -24, -24}, eb[4] = {11, 14, 16, 16};
    for (int i = 0; i < 4; ++i) {
        io.alpha[i] = quantize(a[i], ea[i]);
        io.beta[i] = quantize(b[i], eb[i]);
    }
    return io;
}

Vec3 satPosition(const Ephemeris& e, double t)
{
    const double a = e.sqrtA * e.sqrtA, tk = wrapWeek(t - e.toe);
    const double ek = eccentricAnomaly(e, t);
    const double vk = std::atan2(std::sqrt(1.0 - e.e * e.e) * std::sin(ek), std::cos(ek) - e.e);
    const double phi = vk + e.omega * kGpsPi;
    const double s2 = std::sin(2.0 * phi), c2 = std::cos(2.0 * phi);
    const double uk = phi + e.cus * s2 + e.cuc * c2;
    const double rk = a * (1.0 - e.e * std::cos(ek)) + e.crs * s2 + e.crc * c2;
    const double ik = e.i0 * kGpsPi + e.iDot * kGpsPi * tk + e.cis * s2 + e.cic * c2;
    const double xp = rk * std::cos(uk), yp = rk * std::sin(uk);
    const double om = e.omega0 * kGpsPi + (e.omegaDot * kGpsPi - kOmegaE) * tk - kOmegaE * e.toe;
    return {xp * std::cos(om) - yp * std::cos(ik) * std::sin(om), xp * std::sin(om) + yp * std::cos(ik) * std::cos(om),
            yp * std::sin(ik)};
}

double satClock(const Ephemeris& e, double t)
{
    const double dt = wrapWeek(t - e.toc);
    return e.af0 + e.af1 * dt + e.af2 * dt * dt + kRelF * e.e * e.sqrtA * std::sin(eccentricAnomaly(e, t)) - e.tgd;
}

Vec3 llhToEcef(double lat, double lon, double h)
{
    const double e2 = kWgsF * (2.0 - kWgsF), s = std::sin(lat), c = std::cos(lat);
    const double n = kWgsA / std::sqrt(1.0 - e2 * s * s);
    return {(n + h) * c * std::cos(lon), (n + h) * c * std::sin(lon), (n * (1.0 - e2) + h) * s};
}

void ecefToLlh(const Vec3& p, double* lat, double* lon, double* h)
{
    const double e2 = kWgsF * (2.0 - kWgsF), r = std::hypot(p.x, p.y);
    double phi = std::atan2(p.z, r * (1.0 - e2)), s = std::sin(phi);
    for (int i = 0; i < 10; ++i) {      // each pass gains more than two digits near the surface
        phi = std::atan2(p.z + e2 * kWgsA / std::sqrt(1.0 - e2 * s * s) * s, r);
        s = std::sin(phi);
    }
    *lat = phi;
    *lon = std::atan2(p.y, p.x);
    *h = r * std::cos(phi) + p.z * s - kWgsA * std::sqrt(1.0 - e2 * s * s);
}

Vec3 enuToEcef(double lat, double lon, double e, double n, double u)
{
    const double sp = std::sin(lat), cp = std::cos(lat), sl = std::sin(lon), cl = std::cos(lon);
    return {-sl * e - sp * cl * n + cp * cl * u, cl * e - sp * sl * n + cp * sl * u, cp * n + sp * u};
}

void azEl(double lat, double lon, const Vec3& rx, const Vec3& sat, double* az, double* el)
{
    const Vec3 d{sat.x - rx.x, sat.y - rx.y, sat.z - rx.z};
    const double sp = std::sin(lat), cp = std::cos(lat), sl = std::sin(lon), cl = std::cos(lon);
    const double e = -sl * d.x + cl * d.y;
    const double n = -sp * cl * d.x - sp * sl * d.y + cp * d.z;
    const double u = cp * cl * d.x + cp * sl * d.y + sp * d.z;
    *az = std::atan2(e, n);
    if (*az < 0) *az += 2.0 * kPi;
    *el = std::asin(u / norm(d));
}

double tropoDelay(double lat, double h, double el)
{
    if (el <= 0 || h < -100.0 || h > 1e4) return 0.0;
    const double hgt = std::max(h, 0.0);
    const double pres = 1013.25 * std::pow(1.0 - 2.2557e-5 * hgt, 5.2568);
    const double temp = 15.0 - 6.5e-3 * hgt + 273.16;
    const double wet = 6.108 * 0.5 * std::exp((17.15 * temp - 4684.0) / (temp - 38.45));
    const double map = 1.0 / std::sin(el);
    return (0.0022768 * pres / (1.0 - 0.00266 * std::cos(2.0 * lat) - 0.00028 * hgt / 1e3)
            + 0.002277 * (1255.0 / temp + 0.05) * wet) * map;
}

double ionoDelay(const Iono& io, double lat, double lon, double az, double el, double tow)
{
    if (el <= 0) return 0.0;
    const double e = el / kGpsPi;
    const double psi = 0.0137 / (e + 0.11) - 0.022;
    const double phiI = std::clamp(lat / kGpsPi + psi * std::cos(az), -0.416, 0.416);
    const double lamI = lon / kGpsPi + psi * std::sin(az) / std::cos(phiI * kGpsPi);
    const double phiM = phiI + 0.064 * std::cos((lamI - 1.617) * kGpsPi);
    double t = std::fmod(43200.0 * lamI + tow, 86400.0);
    if (t < 0) t += 86400.0;
    const double f = 1.0 + 16.0 * std::pow(0.53 - e, 3);
    double amp = 0, per = 0;
    for (int i = 3; i >= 0; --i) {
        amp = amp * phiM + io.alpha[i];
        per = per * phiM + io.beta[i];
    }
    amp = std::max(amp, 0.0);
    per = std::max(per, 72000.0);
    const double x = 2.0 * kPi * (t - 50400.0) / per;
    const double delay = std::fabs(x) < 1.57 ? 5e-9 + amp * (1.0 - x * x / 2.0 + x * x * x * x / 24.0) : 5e-9;
    return kC * f * delay;
}

std::array<int8_t, 1023> caCode(int prn)
{
    static const int taps[32][2] = {{2, 6}, {3, 7}, {4, 8}, {5, 9}, {1, 9}, {2, 10}, {1, 8}, {2, 9}, {3, 10}, {2, 3}, {3, 4},
                                    {5, 6}, {6, 7}, {7, 8}, {8, 9}, {9, 10}, {1, 4}, {2, 5}, {3, 6}, {4, 7}, {5, 8}, {6, 9},
                                    {1, 3}, {4, 6}, {5, 7}, {6, 8}, {7, 9}, {8, 10}, {1, 6}, {2, 7}, {3, 8}, {4, 9}};
    int g1[11], g2[11];
    std::fill(g1, g1 + 11, 1);
    std::fill(g2, g2 + 11, 1);
    std::array<int8_t, 1023> code{};
    for (int i = 0; i < 1023; ++i) {
        const int chip = g1[10] ^ g2[taps[prn - 1][0]] ^ g2[taps[prn - 1][1]];
        code[size_t(i)] = chip ? -1 : 1;
        const int f1 = g1[3] ^ g1[10];
        const int f2 = g2[2] ^ g2[3] ^ g2[6] ^ g2[8] ^ g2[9] ^ g2[10];
        for (int k = 10; k > 1; --k) {
            g1[k] = g1[k - 1];
            g2[k] = g2[k - 1];
        }
        g1[1] = f1;
        g2[1] = f2;
    }
    return code;
}

} // namespace sim
