#pragma once

#include "Gps.h"

#include <QString>

#include <vector>

namespace sim {

// Reads a YUMA almanac, sorted by PRN. YUMA counts weeks modulo 1024, so each becomes the full week nearest nearWeek.
// Returns an error message, empty on success.
QString readYuma(const QString& path, int nearWeek, std::vector<AlmanacEntry>* out);

// The GPS week of the current date
int gpsWeekNow();

} // namespace sim
