#include "Lnav.h"

#include <cmath>
#include <initializer_list>

namespace sim {

namespace {

uint32_t field(double value, int exponent, int bits)
{
    const long long v = std::llround(std::ldexp(value, -exponent));
    return uint32_t(v) & (bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u);
}

// Exclusive-or of source bits d1..d24
uint32_t xorBits(uint32_t d, std::initializer_list<int> which)
{
    uint32_t p = 0;
    for (int i : which) p ^= (d >> (24 - i)) & 1;
    return p;
}

// One word from its 24 source bits, given D29 and D30 of the word before (IS-GPS-200 Table 20-XIV)
uint32_t encodeWord(uint32_t d, uint32_t last)
{
    const uint32_t d29 = (last >> 1) & 1, d30 = last & 1;
    const uint32_t p1 = d29 ^ xorBits(d, {1, 2, 3, 5, 6, 10, 11, 12, 13, 14, 17, 18, 20, 23});
    const uint32_t p2 = d30 ^ xorBits(d, {2, 3, 4, 6, 7, 11, 12, 13, 14, 15, 18, 19, 21, 24});
    const uint32_t p3 = d29 ^ xorBits(d, {1, 3, 4, 5, 7, 8, 12, 13, 14, 15, 16, 19, 20, 22});
    const uint32_t p4 = d30 ^ xorBits(d, {2, 4, 5, 6, 8, 9, 13, 14, 15, 16, 17, 20, 21, 23});
    const uint32_t p5 = d30 ^ xorBits(d, {1, 3, 5, 6, 7, 9, 10, 14, 15, 16, 17, 18, 21, 22, 24});
    const uint32_t p6 = d29 ^ xorBits(d, {3, 5, 6, 8, 9, 10, 11, 13, 15, 19, 22, 23, 24});
    const uint32_t data = d30 ? (~d & 0xFFFFFF) : d;
    return (data << 6) | (p1 << 5) | (p2 << 4) | (p3 << 3) | (p4 << 2) | (p5 << 1) | p6;
}

// Words 2 and 10 end in two bits chosen to make the last two parity bits zero
uint32_t encodeWordEndingInZero(uint32_t d22, uint32_t last)
{
    for (uint32_t t = 0; t < 4; ++t) {
        const uint32_t word = encodeWord((d22 << 2) | t, last);
        if ((word & 3) == 0) return word;
    }
    return encodeWord(d22 << 2, last);
}

} // namespace

LnavEncoder::LnavEncoder(const Ephemeris& eph, const std::vector<Ephemeris>& all, const Iono& iono, int leapSeconds)
    : e_(eph), all_(&all), iono_(iono), leapSeconds_(leapSeconds)
{
}

// w[n] is word n+1: 24 source bits, or 22 for word 10 (w[9])
void LnavEncoder::payload(int number, int page, uint32_t* w) const
{
    const Ephemeris& e = e_;
    const auto hi8 = [](uint32_t v) { return (v >> 24) & 0xFF; };
    const auto lo24 = [](uint32_t v) { return v & 0xFFFFFF; };
    if (number == 1) {
        w[2] = (uint32_t(e.week % 1024) << 14) | (1u << 12) | (uint32_t(e.ura) << 8) | ((uint32_t(e.iodc) >> 8) & 3);
        w[6] = field(e.tgd, -31, 8);
        w[7] = (uint32_t(e.iodc & 0xFF) << 16) | field(e.toc, 4, 16);
        w[8] = (field(e.af2, -55, 8) << 16) | field(e.af1, -43, 16);
        w[9] = field(e.af0, -31, 22);
    } else if (number == 2) {
        const uint32_t m0 = field(e.m0, -31, 32), ecc = field(e.e, -33, 32), sqa = field(e.sqrtA, -19, 32);
        w[2] = (uint32_t(e.iode & 0xFF) << 16) | field(e.crs, -5, 16);
        w[3] = (field(e.deltaN, -43, 16) << 8) | hi8(m0);
        w[4] = lo24(m0);
        w[5] = (field(e.cuc, -29, 16) << 8) | hi8(ecc);
        w[6] = lo24(ecc);
        w[7] = (field(e.cus, -29, 16) << 8) | hi8(sqa);
        w[8] = lo24(sqa);
        w[9] = field(e.toe, 4, 16) << 6;
    } else if (number == 3) {
        const uint32_t om = field(e.omega0, -31, 32), i0 = field(e.i0, -31, 32), arg = field(e.omega, -31, 32);
        w[2] = (field(e.cic, -29, 16) << 8) | hi8(om);
        w[3] = lo24(om);
        w[4] = (field(e.cis, -29, 16) << 8) | hi8(i0);
        w[5] = lo24(i0);
        w[6] = (field(e.crc, -5, 16) << 8) | hi8(arg);
        w[7] = lo24(arg);
        w[8] = field(e.omegaDot, -43, 24);
        w[9] = (uint32_t(e.iode & 0xFF) << 14) | field(e.iDot, -43, 14);
    } else {
        // Page assignment of IS-GPS-200 Table 20-V. Spare pages carry the alternating pattern with a dummy SVID.
        int almPrn = 0, svid = number == 4 ? 57 : 51;
        if (number == 5 && page <= 24) almPrn = page;
        else if (number == 4 && page >= 2 && page <= 5) almPrn = 23 + page;
        else if (number == 4 && page >= 7 && page <= 10) almPrn = 22 + page;
        // A real satellite sends page 18 every 12.5 min, too rarely for a short run: this one sends it every other frame
        else if (number == 4 && (page == 18 || ((page - 1) % 2 == 0 && almPrn == 0))) svid = 56;
        for (int i = 3; i < 9; ++i) w[i] = 0xAAAAAA;
        w[9] = 0x2AAAAA;
        w[2] = 0xAAAA;
        if (almPrn && (*all_)[size_t(almPrn - 1)].prn == 0) {
            svid = 0;                                                  // a dummy SV in place of a missing satellite
        } else if (almPrn) {
            const Ephemeris& a = (*all_)[size_t(almPrn - 1)];
            svid = almPrn;
            // The almanac's reference time is a multiple of 4096 s: carry the orbit and clock there from toe
            const double toa = std::round(a.toe / 4096.0) * 4096.0, dt = toa - a.toe;
            const double n = std::sqrt(kMu / std::pow(a.sqrtA, 6)) / kGpsPi + a.deltaN;      // semicircles/s
            const uint32_t af0 = field(a.af0 + a.af1 * dt, -20, 11);
            w[2] = field(a.e, -21, 16);
            w[3] = (field(toa, 12, 8) << 16) | field(a.i0 - 0.3, -19, 16);
            w[4] = field(a.omegaDot, -38, 16) << 8;
            w[5] = field(a.sqrtA, -11, 24);
            w[6] = field(a.omega0 + a.omegaDot * dt, -23, 24);      // the 24 bits wrap it to +/-1 semicircle
            w[7] = field(a.omega, -23, 24);
            w[8] = field(a.m0 + n * dt, -23, 24);
            w[9] = (((af0 >> 3) & 0xFF) << 14) | (field(a.af1, -38, 11) << 3) | (af0 & 7);
        } else if (svid == 56) {
            w[2] = (field(iono_.alpha[0], -30, 8) << 8) | field(iono_.alpha[1], -27, 8);
            w[3] = (field(iono_.alpha[2], -24, 8) << 16) | (field(iono_.alpha[3], -24, 8) << 8) | field(iono_.beta[0], 11, 8);
            w[4] = (field(iono_.beta[1], 14, 8) << 16) | (field(iono_.beta[2], 16, 8) << 8) | field(iono_.beta[3], 16, 8);
            w[5] = 0;                                                  // A1
            w[6] = 0;                                                  // A0
            w[7] = uint32_t(e.week & 0xFF);                            // A0, t_ot, WN_t
            // Leap seconds, WN_LSF, DN, and the same leap seconds after them: none pending
            const uint32_t leap = uint32_t(leapSeconds_ & 0xFF);
            w[8] = (leap << 16) | (uint32_t(e.week & 0xFF) << 8) | 7u;
            w[9] = leap << 14;
        }
        w[2] |= (1u << 22) | (uint32_t(svid) << 16);                   // data ID, SVID
    }
}

std::array<uint32_t, 10> LnavEncoder::subframe(int index) const
{
    const int number = index % 5 + 1, page = (index / 5) % 25 + 1;
    uint32_t src[10] = {};
    src[0] = 0x8B0000;
    // Truncated time of week of the next subframe, alert, anti-spoof and the subframe number
    src[1] = (uint32_t((index + 1) % 100800) << 5) | uint32_t(number);
    payload(number, page, src);

    std::array<uint32_t, 10> words{};
    uint32_t last = 0;
    for (int i = 0; i < 10; ++i) {
        words[size_t(i)] = i == 1 || i == 9 ? encodeWordEndingInZero(src[i], last) : encodeWord(src[i], last);
        last = words[size_t(i)];
    }
    return words;
}

int LnavEncoder::bit(long long bitIndex)
{
    const long long sf = bitIndex / 300;
    if (sf != cached_) {
        const auto words = subframe(int(sf % 100800));
        for (int w = 0; w < 10; ++w)
            for (int b = 0; b < 30; ++b) bits_[size_t(w * 30 + b)] = (words[size_t(w)] >> (29 - b)) & 1;
        cached_ = sf;
    }
    return bits_[size_t(bitIndex % 300)];
}

} // namespace sim
