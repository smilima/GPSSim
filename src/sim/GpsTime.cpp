#include "GpsTime.h"

#include "Gps.h"

#include <QTimeZone>

#include <algorithm>

namespace sim {

namespace {

const QDateTime& gpsEpoch()
{
    static const QDateTime epoch(QDate(1980, 1, 6), QTime(0, 0), QTimeZone::UTC);
    return epoch;
}

// The UTC days from whose start GPS - UTC has had this many seconds (IERS Bulletin C; none announced after 2017)
struct Leap
{
    int y, m, d, seconds;
};
const Leap kLeaps[] = {{1981, 7, 1, 1},  {1982, 7, 1, 2},  {1983, 7, 1, 3},  {1985, 7, 1, 4},  {1988, 1, 1, 5},
                       {1990, 1, 1, 6},  {1991, 1, 1, 7},  {1992, 7, 1, 8},  {1993, 7, 1, 9},  {1994, 7, 1, 10},
                       {1996, 1, 1, 11}, {1997, 7, 1, 12}, {1999, 1, 1, 13}, {2006, 1, 1, 14}, {2009, 1, 1, 15},
                       {2012, 7, 1, 16}, {2015, 7, 1, 17}, {2017, 1, 1, 18}};

// Seconds from the GPS epoch to the start of that UTC day, on the UTC scale
qint64 utcSeconds(const Leap& l)
{
    return gpsEpoch().secsTo(QDateTime(QDate(l.y, l.m, l.d), QTime(0, 0), QTimeZone::UTC));
}

} // namespace

int leapSeconds(int week, int tow)
{
    const qint64 gps = qint64(week) * kSecondsPerWeek + tow;
    int ls = 0;
    for (const Leap& l : kLeaps)
        if (gps >= utcSeconds(l) + l.seconds) ls = l.seconds;      // that day starts this much later in GPS time
    return ls;
}

void utcToGps(const QDateTime& utc, int* week, int* tow)
{
    const qint64 seconds = gpsEpoch().secsTo(utc);
    int ls = 0;
    for (const Leap& l : kLeaps)
        if (seconds >= utcSeconds(l)) ls = l.seconds;
    const qint64 gps = std::max<qint64>(0, seconds + ls);
    *week = int(gps / kSecondsPerWeek);
    *tow = int(gps % kSecondsPerWeek);
}

QDateTime gpsToUtc(int week, int tow)
{
    return gpsEpoch().addSecs(qint64(week) * kSecondsPerWeek + tow - leapSeconds(week, tow));
}

int gpsWeekNow()
{
    int week = 0, tow = 0;
    utcToGps(QDateTime::currentDateTimeUtc(), &week, &tow);
    return week;
}

} // namespace sim
