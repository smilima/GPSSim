#pragma once

#include <QDateTime>

namespace sim {

// GPS time runs ahead of UTC by the leap seconds added since 1980 (18 since the start of 2017)
int leapSeconds(int week, int tow);

// A UTC date and time as GPS week and time of week, and back. Times before 6 January 1980 come out as week 0.
void utcToGps(const QDateTime& utc, int* week, int* tow);
QDateTime gpsToUtc(int week, int tow);

// The GPS week of the current date
int gpsWeekNow();

} // namespace sim
