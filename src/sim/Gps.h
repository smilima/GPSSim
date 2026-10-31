#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sim {

constexpr double kC = 299792458.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kGpsPi = 3.1415926535898;      // value mandated by IS-GPS-200
constexpr double kMu = 3.986005e14;
constexpr double kOmegaE = 7.2921151467e-5;
constexpr double kRelF = -4.442807633e-10;
constexpr double kL1 = 1575.42e6;
constexpr double kChipRate = 1.023e6;
constexpr double kWgsA = 6378137.0;
constexpr double kWgsF = 1.0 / 298.257223563;
constexpr int kSecondsPerWeek = 604800;
constexpr int kNumSats = 32;

struct Vec3
{
    double x = 0, y = 0, z = 0;
};

// Broadcast parameters, in the units of the navigation message (angles in semicircles). Every value is exactly
// representable in the message, so what is transmitted is what is simulated.
struct Ephemeris
{
    int prn = 0, week = 0, iodc = 0, iode = 0, ura = 0;
    double toc = 0, toe = 0, af0 = 0, af1 = 0, af2 = 0, tgd = 0;
    double crs = 0, deltaN = 0, m0 = 0, cuc = 0, e = 0, cus = 0, sqrtA = 0;
    double cic = 0, omega0 = 0, cis = 0, i0 = 0, crc = 0, omega = 0, omegaDot = 0, iDot = 0;
};

struct Iono
{
    double alpha[4] = {0, 0, 0, 0};
    double beta[4] = {0, 0, 0, 0};
};

// A nominal 32-satellite constellation in six planes, with ephemeris reference time toe.
std::vector<Ephemeris> makeConstellation(int week, int toe);
Iono defaultIono();

// IS-GPS-200 Table 20-IV. Position in the ECEF frame of time t.
Vec3 satPosition(const Ephemeris& e, double t);
// Satellite clock error at GPS time t, with the relativistic term and T_GD: t_sv = t + result.
double satClock(const Ephemeris& e, double t);

Vec3 llhToEcef(double lat, double lon, double h);
Vec3 enuToEcef(double lat, double lon, double e, double n, double u);
void azEl(double lat, double lon, const Vec3& rx, const Vec3& sat, double* az, double* el);

double tropoDelay(double lat, double h, double el);      // m
double ionoDelay(const Iono& io, double lat, double lon, double az, double el, double tow);      // m on L1

// C/A code as +1/-1 chips
std::array<int8_t, 1023> caCode(int prn);

} // namespace sim
