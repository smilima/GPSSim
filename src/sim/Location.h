#pragma once

#include <QString>

namespace sim {

// The ways a position is written on maps
enum class LocationFormat { Degrees, DegreesMinutes, DegreesMinutesSeconds, Utm, Mgrs, Ecef };

// A position as text: latitude and longitude in degrees, h in metres above the WGS-84 ellipsoid. UTM and MGRS reach
// from 80° S to 84° N, so nearer the poles the position comes out in degrees.
QString formatLocation(LocationFormat format, double latDeg, double lonDeg, double h);

// Reads a position written in any of those ways:
//   40.014984, -105.270546        40° 00.899' N, 105° 16.233' W        40°00'53.9"N 105°16'14.0"W
//   13T 476911 4429455            13TDE7691129455                      -1288678.6, -4720147.7, 4080320.9
// A height may follow, in metres or in feet ("1650 m", "5413 ft"); without one *h is kept. An MGRS position is the
// south-west corner of its square. *format takes the way the position was written. Returns an error message, empty
// on success.
QString parseLocation(const QString& text, double* latDeg, double* lonDeg, double* h, LocationFormat* format = nullptr);

} // namespace sim
