#pragma once

#include "Gps.h"

#include <array>
#include <vector>

namespace sim {

// The LNAV message of one satellite: subframes 1-3 from its ephemeris, ionosphere in subframe 4 page 18 and the almanac
// of the whole constellation in the other pages.
class LnavEncoder
{
public:
    LnavEncoder(const Ephemeris& eph, const std::vector<Ephemeris>& all, const Iono& iono);

    // The ten 30-bit words of the subframe that starts at subframe count 'index' (time of week / 6)
    std::array<uint32_t, 10> subframe(int index) const;
    // The bit (0/1) transmitted during the given 20 ms period of the week
    int bit(long long bitIndex);

private:
    void payload(int number, int page, uint32_t* w) const;

    Ephemeris e_;
    const std::vector<Ephemeris>* all_;
    Iono iono_;
    long long cached_ = -1;
    std::array<uint8_t, 300> bits_{};
};

} // namespace sim
