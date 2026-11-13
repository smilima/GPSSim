#pragma once

#include "Gps.h"

#include <QString>

#include <vector>

namespace sim {

// Where the US Coast Guard Navigation Center keeps the current YUMA almanac
inline constexpr char kCurrentYumaUrl[] = "https://www.navcen.uscg.gov/sites/default/files/gps/almanac/current_yuma.alm";

// Reads a YUMA almanac, sorted by PRN. YUMA counts weeks modulo 1024, so each becomes the full week nearest nearWeek.
// Returns an error message, empty on success.
QString readYuma(const QString& path, int nearWeek, std::vector<AlmanacEntry>* out);

// The same from the text of one, which the messages call name
QString parseYuma(const QByteArray& yuma, const QString& name, int nearWeek, std::vector<AlmanacEntry>* out);

} // namespace sim
